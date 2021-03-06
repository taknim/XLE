// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Material.h"
#include "../Metal/State.h"      // (just for Blend/BlendOp enum members... maybe we need a higher level version of these enums?)
#include "../../Assets/AssetUtils.h"
#include "../../Assets/IntermediateResources.h"
#include "../../Assets/BlockSerializer.h"
#include "../../Assets/ChunkFile.h"
#include "../../ConsoleRig/Log.h"
#include "../../Utility/Streams/Data.h"
#include "../../Utility/Conversion.h"
#include "../../Utility/Streams/FileUtils.h"
#include "../../Utility/Streams/PathUtils.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/MemoryUtils.h"

namespace RenderCore { namespace Assets
{

///////////////////////////////////////////////////////////////////////////////////////////////////
    
    void ResourceBinding::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        Serialization::Serialize(serializer, _bindHash);
        Serialization::Serialize(serializer, _resourceName);
    }
    
    void ResolvedMaterial::Serialize(Serialization::NascentBlockSerializer& serializer) const
    {
        Serialization::Serialize(serializer, _bindings);
        Serialization::Serialize(serializer, _matParams);
        Serialization::Serialize(serializer, _stateSet.GetHash());
        Serialization::Serialize(serializer, _constants);
    }

    RenderStateSet::RenderStateSet()
    {
        _doubleSided = false;
        _wireframe = false;
        _writeMask = 0xf;
        _deferredBlend = DeferredBlend::Opaque;
        _depthBias = 0;
        _flag = 0;
        
        _forwardBlendSrc = Metal::Blend::One;
        _forwardBlendDst = Metal::Blend::Zero;
        _forwardBlendOp = Metal::BlendOp::NoBlending;
    }

    uint64 RenderStateSet::GetHash() const
    {
        static_assert(sizeof(*this) == sizeof(uint64), "expecting StateSet to be 64 bits long");
        return *(const uint64*)this;
    }

    ResolvedMaterial::ResolvedMaterial() {}

    ResolvedMaterial::ResolvedMaterial(ResolvedMaterial&& moveFrom)
    : _bindings(std::move(moveFrom._bindings))
    , _matParams(std::move(moveFrom._matParams))
    , _stateSet(moveFrom._stateSet)
    , _constants(std::move(moveFrom._constants))
    {}

    ResolvedMaterial& ResolvedMaterial::operator=(ResolvedMaterial&& moveFrom)
    {
        _bindings = std::move(moveFrom._bindings);
        _matParams = std::move(moveFrom._matParams);
        _stateSet = moveFrom._stateSet;
        _constants = std::move(moveFrom._constants);
        return *this;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    static const std::pair<Metal::Blend::Enum, const char*> s_blendNames[] =
    {
        std::make_pair(Metal::Blend::Zero, "zero"),
        std::make_pair(Metal::Blend::One, "one"),
            
        std::make_pair(Metal::Blend::SrcColor, "srccolor"),
        std::make_pair(Metal::Blend::InvSrcColor, "invsrccolor"),
        std::make_pair(Metal::Blend::DestColor, "destcolor"),
        std::make_pair(Metal::Blend::InvDestColor, "invdestcolor"),

        std::make_pair(Metal::Blend::SrcAlpha, "srcalpha"),
        std::make_pair(Metal::Blend::InvSrcAlpha, "invsrcalpha"),
        std::make_pair(Metal::Blend::DestAlpha, "destalpha"),
        std::make_pair(Metal::Blend::InvDestAlpha, "invdestalpha"),
    };

    static const std::pair<Metal::BlendOp::Enum, const char*> s_blendOpNames[] =
    {
        std::make_pair(Metal::BlendOp::NoBlending, "noblending"),
        std::make_pair(Metal::BlendOp::NoBlending, "none"),
        std::make_pair(Metal::BlendOp::NoBlending, "false"),

        std::make_pair(Metal::BlendOp::Add, "add"),
        std::make_pair(Metal::BlendOp::Subtract, "subtract"),
        std::make_pair(Metal::BlendOp::RevSubtract, "revSubtract"),
        std::make_pair(Metal::BlendOp::Min, "min"),
        std::make_pair(Metal::BlendOp::Max, "max")
    };
    
    static Metal::Blend::Enum DeserializeBlend(const Data* source, const char name[])
    {
        if (source) {
            auto* child = source->ChildWithValue(name);
            if (child && child->child && child->child->value) {
                const char* value = child->child->value;
                for (unsigned c=0; c<dimof(s_blendNames); ++c) {
                    if (!XlCompareStringI(value, s_blendNames[c].second)) {
                        return s_blendNames[c].first;
                    }
                }
                return (Metal::Blend::Enum)XlAtoI32(value);
            }
        }

        return Metal::Blend::Zero;
    }

    static Metal::BlendOp::Enum DeserializeBlendOp(const Data* source, const char name[])
    {
        if (source) {
            auto* child = source->ChildWithValue(name);
            if (child && child->child && child->child->value) {
                const char* value = child->child->value;
                for (unsigned c=0; c<dimof(s_blendOpNames); ++c) {
                    if (!XlCompareStringI(value, s_blendOpNames[c].second)) {
                        return s_blendOpNames[c].first;
                    }
                }
                return (Metal::BlendOp::Enum)XlAtoI32(value);
            }
        }

        return Metal::BlendOp::NoBlending;
    }

    static RenderStateSet DeserializeStateSet(const Data& src)
    {
        RenderStateSet result;
        {
            auto* child = src.ChildWithValue("DoubleSided");
            if (child && child->child && child->child->value) {
                result._doubleSided = Conversion::Convert<bool>((const char*)child->child->value);
                result._flag |= RenderStateSet::Flag::DoubleSided;
            }
        }
        {
            auto* child = src.ChildWithValue("Wireframe");
            if (child && child->child && child->child->value) {
                result._wireframe = Conversion::Convert<bool>((const char*)child->child->value);
                result._flag |= RenderStateSet::Flag::Wireframe;
            }
        }
        {
            auto* child = src.ChildWithValue("WriteMask");
            if (child && child->child && child->child->value) {
                result._writeMask = child->child->IntValue();
                result._flag |= RenderStateSet::Flag::WriteMask;
            }
        }
        {
            auto* child = src.ChildWithValue("DeferredBlend");
            if (child && child->child && child->child->value) {
                if (XlCompareStringI(child->child->value, "decal")) {
                    result._deferredBlend = RenderStateSet::DeferredBlend::Decal;
                } else {
                    result._deferredBlend = RenderStateSet::DeferredBlend::Opaque;
                }
                result._flag |= RenderStateSet::Flag::DeferredBlend;
            }
        }
        {
            auto* child = src.ChildWithValue("DepthBias");
            if (child && child->child && child->child->value) {
                result._depthBias = child->child->IntValue();
                result._flag |= RenderStateSet::Flag::DepthBias;
            }
        }
        {
            auto* child = src.ChildWithValue("ForwardBlend");
            if (child && child->child) {
                result._forwardBlendSrc = DeserializeBlend(child, "Src");
                result._forwardBlendDst = DeserializeBlend(child, "Dst");
                result._forwardBlendOp = DeserializeBlendOp(child, "Op");
                result._flag |= RenderStateSet::Flag::ForwardBlend;
            }
        }
        return result;
    }

    static const char* AsString(RenderStateSet::DeferredBlend::Enum blend)
    {
        switch (blend) {
        case RenderStateSet::DeferredBlend::Decal: return "decal";
        default:
        case RenderStateSet::DeferredBlend::Opaque: return "opaque";
        }
    }

    static const char* AsString(Metal::Blend::Enum input)
    {
        for (unsigned c=0; c<dimof(s_blendNames); ++c) {
            if (s_blendNames[c].first == input) {
                return s_blendNames[c].second;
            }
        }
        return "one";
    }

    static const char* AsString(Metal::BlendOp::Enum input)
    {
        for (unsigned c=0; c<dimof(s_blendOpNames); ++c) {
            if (s_blendOpNames[c].first == input) {
                return s_blendOpNames[c].second;
            }
        }
        return "noblending";
    }

    std::unique_ptr<Data> SerializeStateSet(const char name[], const RenderStateSet& stateSet)
    {
        // opposite of DeserializeStateSet... create a serialized form of these states
        auto result = std::make_unique<Data>(name);
        if (stateSet._flag & RenderStateSet::Flag::DoubleSided) {
            result->SetAttribute("DoubleSided", stateSet._doubleSided);
        }

        if (stateSet._flag & RenderStateSet::Flag::Wireframe) {
            result->SetAttribute("Wireframe", stateSet._wireframe);
        }

        if (stateSet._flag & RenderStateSet::Flag::WriteMask) {
            result->SetAttribute("WriteMask", stateSet._writeMask);
        }

        if (stateSet._flag & RenderStateSet::Flag::DeferredBlend) {
            result->SetAttribute("DeferredBlend", AsString(stateSet._deferredBlend));
        }

        if (stateSet._flag & RenderStateSet::Flag::DepthBias) {
            result->SetAttribute("DepthBias", stateSet._depthBias);
        }

        if (stateSet._flag & RenderStateSet::Flag::ForwardBlend) {
            auto block = std::make_unique<Data>("ForwardBlend");
            block->SetAttribute("Src", AsString(stateSet._forwardBlendSrc));
            block->SetAttribute("Dst", AsString(stateSet._forwardBlendDst));
            block->SetAttribute("Op", AsString(stateSet._forwardBlendOp));
            result->Add(block.release());
        }

        return std::move(result);
    }

    static Assets::RenderStateSet Merge(
        Assets::RenderStateSet underride,
        Assets::RenderStateSet override)
    {
        typedef Assets::RenderStateSet StateSet;
        StateSet result = underride;
        if (override._flag & StateSet::Flag::DoubleSided) {
            result._doubleSided = override._doubleSided;
            result._flag |= StateSet::Flag::DoubleSided;
        }
        if (override._flag & StateSet::Flag::Wireframe) {
            result._wireframe = override._wireframe;
            result._flag |= StateSet::Flag::Wireframe;
        }
        if (override._flag & StateSet::Flag::WriteMask) {
            result._writeMask = override._writeMask;
            result._flag |= StateSet::Flag::WriteMask;
        }
        if (override._flag & StateSet::Flag::DeferredBlend) {
            result._deferredBlend = override._deferredBlend;
            result._flag |= StateSet::Flag::DeferredBlend;
        }
        if (override._flag & StateSet::Flag::ForwardBlend) {
            result._forwardBlendSrc = override._forwardBlendSrc;
            result._forwardBlendDst = override._forwardBlendDst;
            result._forwardBlendOp = override._forwardBlendOp;
            result._flag |= StateSet::Flag::ForwardBlend;
        }
        if (override._flag & StateSet::Flag::DepthBias) {
            result._depthBias = override._depthBias;
            result._flag |= StateSet::Flag::DepthBias;
        }
        return result;
    }

    RawMaterial::RawMaterial() {}

    void MakeConcreteRawMaterialFilename(::Assets::ResChar dest[], unsigned dstCount, const ::Assets::ResChar inputName[])
    {
        assert(dest != inputName);
        XlCopyString(dest, dstCount, inputName);

            //  If we're attempting to load from a .dae file, then we need to
            //  instead redirect the query towards the compiled version
        auto ext = XlExtension(dest);
        if (ext && !XlCompareStringI(ext, "dae")) {
            auto& store = ::Assets::CompileAndAsyncManager::GetInstance().GetIntermediateStore();
            XlCatString(dest, dstCount, "-rawmat");
            store.MakeIntermediateName(dest, dstCount, dest);
        }
    }

    uint64 MakeMaterialGuid(const char name[])
    {
            //  If the material name is just a number, then we will use that
            //  as the guid. Otherwise we hash the name.
        const char* parseEnd = nullptr;
        uint64 hashId = XlAtoI64(name, &parseEnd, 16);
        if (!parseEnd || *parseEnd != '\0' ) { hashId = Hash64(name); }
        return hashId;
    }

    RawMaterial::RawMatSplitName::RawMatSplitName() {}

    RawMaterial::RawMatSplitName::RawMatSplitName(const ::Assets::ResChar initialiser[])
    {
            // We're expecting an initialiser of the format "filename:setting"
        auto colon = XlFindCharReverse(initialiser, ':');
        if (!colon)
            ThrowException(::Assets::Exceptions::InvalidResource(initialiser, ""));

        ::Assets::ResChar rawFilename[MaxPath];
        XlCopyNString(rawFilename, initialiser, colon - initialiser);

        ::Assets::ResChar concreteFilename[MaxPath];
        MakeConcreteRawMaterialFilename(concreteFilename, dimof(concreteFilename), rawFilename);

        _settingName = colon+1;
        _concreteFilename  = concreteFilename;
        _initializerFilename = rawFilename;
    }

    RawMaterial::RawMaterial(const ::Assets::ResChar initialiser[])
    {
        _splitName = RawMatSplitName(initialiser);
        auto searchRules = ::Assets::DefaultDirectorySearchRules(_splitName._initializerFilename.c_str());
        
        size_t sourceFileSize = 0;
        auto sourceFile = LoadFileAsMemoryBlock(_splitName._concreteFilename.c_str(), &sourceFileSize);
        if (!sourceFile)
            ThrowException(::Assets::Exceptions::InvalidResource(initialiser, 
                StringMeld<128>() << "Missing or empty file: " << _splitName._concreteFilename));

        Data data;
        data.Load((const char*)sourceFile.get(), (int)sourceFileSize);

        auto source = data.ChildWithValue(_splitName._settingName.c_str());
        if (!source) {
            StringMeld<64> hashedName;
            hashedName << std::hex << Hash64(_splitName._settingName);
            source = data.ChildWithValue(hashedName);
        }

        if (!source)
            ThrowException(::Assets::Exceptions::InvalidResource(initialiser, 
                StringMeld<256>() << "Missing material configuration: " << _splitName._settingName));

        _depVal = std::make_shared<::Assets::DependencyValidation>();

                // first, load inherited settings.
        auto* inheritList = source->ChildWithValue("Inherit");
        if (inheritList) {
            for (auto i=inheritList->child; i; i=i->next) {
                auto* colon = XlFindCharReverse(i->value, ':');
                if (colon) {
                    ::Assets::ResChar resolvedFile[MaxPath];
                    XlCopyNString(resolvedFile, i->value, colon-i->value);
                    if (!XlExtension(resolvedFile))
                        XlCatString(resolvedFile, dimof(resolvedFile), ".material");
                    searchRules.ResolveFile(resolvedFile, dimof(resolvedFile), resolvedFile);
                    XlNormalizePath(resolvedFile, dimof(resolvedFile), resolvedFile);

                    StringMeld<MaxPath, ::Assets::ResChar> finalRawMatName;
                    finalRawMatName << resolvedFile << colon;

                    _inherit.push_back(ResString(finalRawMatName));

                    TRY {
                        RegisterAssetDependency(
                            _depVal, 
                            &::Assets::GetAssetDep<RawMaterial>((const ::Assets::ResChar*)finalRawMatName).GetDependencyValidation());
                    } CATCH (...) {} CATCH_END
                }
            }
        }

            //  Load ShaderParams & ResourceBindings & Constants

        const auto* p = source->ChildWithValue("ShaderParams");
        if (p) {
            for (auto child=p->child; child; child=child->next) {
                if (child->ChildAt(0)) {
                    _matParamBox.SetParameter(child->StrValue(), child->ChildAt(0)->StrValue());
                }
            }
        }

        const auto* c = source->ChildWithValue("Constants");
        if (c) {
            for (auto child=c->child; child; child=child->next) {
                if (child->ChildAt(0)) {
                    _constants.SetParameter(child->StrValue(), child->ChildAt(0)->StrValue());
                }
            }
        }

        const auto* resourceBindings = source->ChildWithValue("ResourceBindings");
        if (resourceBindings) {
            for (auto i=resourceBindings->child; i; i=i->next) {
                if (i->value && i->value[0] && i->child) {
                    const char* resource = i->child->value;
                    if (resource && resource[0]) {
                        uint64 hash = ~0ull;

                            //  if we can parse the the name as hex, then well and good...
                            //  otherwise, treat it as a string
                        const char* endptr = nullptr;
                        hash = XlAtoI64(i->value, &endptr, 16);
                        if (!endptr || *endptr != '\0') {
                            hash = Hash64(i->value, &i->value[XlStringLen(i->value)]);
                        }

                        auto i = std::lower_bound(
                            _resourceBindings.begin(), _resourceBindings.end(),
                            hash, ResourceBinding::Compare());

                        if (i!=_resourceBindings.end() && i->_bindHash==hash) {
                            i->_resourceName = resource;
                        } else {
                            _resourceBindings.insert(i, ResourceBinding(hash, resource));
                        }
                    }
                }
            }
        }

            // also load "States" table. This requires a bit more parsing work
        const auto* stateSet = source->ChildWithValue("States");
        if (stateSet) {
            _stateSet = DeserializeStateSet(*stateSet);
        }

        RegisterFileDependency(_depVal, _splitName._concreteFilename.c_str());
    }

    RawMaterial::~RawMaterial() {}

    static std::unique_ptr<Data> WriteStringTable(const char name[], const std::vector<std::pair<const char*, std::string>>& table)
    {
        auto result = std::make_unique<Data>(name);
        for (auto i=table.cbegin(); i!=table.cend(); ++i) {
            result->SetAttribute(i->first, i->second.c_str());
        }
        return std::move(result);
    }

    std::unique_ptr<Data> RawMaterial::SerializeAsData() const
    {
        auto result = std::make_unique<Data>();

        if (!_inherit.empty()) {
            auto inheritBlock = std::make_unique<Data>("Inherit");
            for (auto i=_inherit.cbegin(); i!=_inherit.cend(); ++i) {
                inheritBlock->Add(new Data(i->c_str()));
            }

            result->Add(inheritBlock.release());
        }

        std::vector<std::pair<const char*, std::string>> matParamStringTable;
        _matParamBox.BuildStringTable(matParamStringTable);
        if (!matParamStringTable.empty()) {
            result->Add(WriteStringTable("ShaderParams", matParamStringTable).release());
        }

        std::vector<std::pair<const char*, std::string>> constantsStringTable;
        _constants.BuildStringTable(constantsStringTable);
        if (!constantsStringTable.empty()) {
            result->Add(WriteStringTable("Constants", constantsStringTable).release());
        }

        if (!_resourceBindings.empty()) {
            auto block = std::make_unique<Data>("ResourceBindings");
            for (auto i=_resourceBindings.cbegin(); i!=_resourceBindings.cend(); ++i) {
                block->SetAttribute(StringMeld<64>() << std::hex << i->_bindHash, i->_resourceName.c_str());
            }
            result->Add(block.release());
        }

        result->Add(SerializeStateSet("States", _stateSet).release());
        return std::move(result);
    }

    void RawMaterial::MergeInto(ResolvedMaterial& dest) const
    {
        dest._matParams.MergeIn(_matParamBox);
        dest._stateSet = Merge(dest._stateSet, _stateSet);
        dest._constants.MergeIn(_constants);

        auto desti = dest._bindings.begin();
        for (auto i=_resourceBindings.begin(); i!=_resourceBindings.end(); ++i) {
            desti = std::lower_bound(
                desti, dest._bindings.end(),
                i->_bindHash, ResourceBinding::Compare());
            if (desti!=dest._bindings.end() && desti->_bindHash == i->_bindHash) {
                desti->_resourceName = i->_resourceName;
            } else {
                desti = dest._bindings.insert(desti, *i);
            }
        }
    }

    ResolvedMaterial RawMaterial::Resolve(
        const ::Assets::DirectorySearchRules& searchRules,
        std::vector<::Assets::FileAndTime>* deps) const
    {
            // resolve all of the inheritance options and generate a final 
            // ResolvedMaterial object. We need to start at the bottom of the
            // inheritance tree, and merge in new parameters as we come across them.

        ResolvedMaterial result;
        for (auto i=_inherit.cbegin(); i!=_inherit.cend(); ++i) {
            TRY {
                RawMatSplitName splitName(i->c_str());

                    // add a dependency to this file, even if it doesn't exist
                if (deps) {
                    auto existing = std::find_if(deps->cbegin(), deps->cend(),
                        [&](const ::Assets::FileAndTime& test) 
                        {
                            return !XlCompareStringI(test._filename.c_str(), splitName._concreteFilename.c_str());
                        });
                    if (existing == deps->cend()) {
                        ::Assets::FileAndTime fileAndTime(
                            splitName._concreteFilename, 
                            GetFileModificationTime(splitName._concreteFilename.c_str()));
                        deps->push_back(fileAndTime);
                    }
                }

                auto& rawParams = ::Assets::GetAssetDep<RawMaterial>(i->c_str());
                rawParams.MergeInto(result);
            } 
            CATCH (...) {} 
            CATCH_END
        }

        MergeInto(result);
        if (deps) {
            auto existing = std::find_if(deps->cbegin(), deps->cend(),
                [&](const ::Assets::FileAndTime& test) 
                {
                    return !XlCompareStringI(test._filename.c_str(), _splitName._concreteFilename.c_str());
                });
            if (existing == deps->cend()) {
                ::Assets::FileAndTime fileAndTime(
                    _splitName._concreteFilename, 
                    GetFileModificationTime(_splitName._concreteFilename.c_str()));
                deps->push_back(fileAndTime);
            }
        }

        return result;
    }



}}


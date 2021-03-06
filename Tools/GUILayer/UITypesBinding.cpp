// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma warning(disable:4512)

#include "UITypesBinding.h"
#include "../../PlatformRig/ModelVisualisation.h"
#include "../../RenderCore/Assets/Material.h"
#include "../../RenderCore/Assets/MaterialScaffold.h"
#include "../../Utility/StringFormat.h"
#include <msclr\auto_gcroot.h>
#include <iomanip>

namespace GUILayer
{

///////////////////////////////////////////////////////////////////////////////////////////////////

    class InvalidatePropertyGrid : public OnChangeCallback
    {
    public:
        void    OnChange();

        InvalidatePropertyGrid(PropertyGrid^ linked);
        ~InvalidatePropertyGrid();
    protected:
        msclr::auto_gcroot<PropertyGrid^> _linked;
    };

    void    InvalidatePropertyGrid::OnChange()
    {
        if (_linked.get()) {
            _linked->Refresh();
        }
    }

    InvalidatePropertyGrid::InvalidatePropertyGrid(PropertyGrid^ linked) : _linked(linked) {}
    InvalidatePropertyGrid::~InvalidatePropertyGrid() {}

    void ModelVisSettings::AttachCallback(PropertyGrid^ callback)
    {
        (*_object)->_changeEvent._callbacks.push_back(
            std::shared_ptr<OnChangeCallback>(new InvalidatePropertyGrid(callback)));
    }

    ModelVisSettings^ ModelVisSettings::CreateDefault()
    {
        auto attached = std::make_shared<PlatformRig::ModelVisSettings>();
        return gcnew ModelVisSettings(std::move(attached));
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    System::String^ VisMouseOver::IntersectionPt::get()
    {
        if ((*_object)->_hasMouseOver) {
            return clix::marshalString<clix::E_UTF8>(
                std::string(StringMeld<64>()
                    << std::setprecision(5)
                    << (*_object)->_intersectionPt[0] << ","
                    << (*_object)->_intersectionPt[1] << ","
                    << (*_object)->_intersectionPt[2]));
        } else {
            return "<<no intersection>>";
        }
    }

    unsigned VisMouseOver::DrawCallIndex::get() 
    { 
        if ((*_object)->_hasMouseOver) {
            return (*_object)->_drawCallIndex;
        } else {
            return ~unsigned(0x0);
        }
    }

    System::String^ VisMouseOver::MaterialName::get() 
    {
        auto fullName = FullMaterialName;
        if (fullName) {
            int index = fullName->IndexOf(':');
            return fullName->Substring((index>=0) ? (index+1) : 0);
        }
        return "<<no material>>";
    }

    bool VisMouseOver::HasMouseOver::get()
    {
        return (*_object)->_hasMouseOver;
    }

    System::String^ VisMouseOver::FullMaterialName::get()
    {
        if ((*_object)->_hasMouseOver) {
            auto scaffolds = (*_modelCache)->GetScaffolds((*_modelSettings)->_modelName.c_str());
            if (scaffolds._material) {
                auto matName = scaffolds._material->GetMaterialName((*_object)->_materialGuid);
                if (matName) {
                    return clix::marshalString<clix::E_UTF8>(std::string(matName));
                }
            }
        }
        return nullptr;
    }

    void VisMouseOver::AttachCallback(PropertyGrid^ callback)
    {
        (*_object)->_changeEvent._callbacks.push_back(
            std::shared_ptr<OnChangeCallback>(new InvalidatePropertyGrid(callback)));
    }

    VisMouseOver::VisMouseOver(
        std::shared_ptr<PlatformRig::VisMouseOver> attached,
        std::shared_ptr<PlatformRig::ModelVisSettings> settings,
        std::shared_ptr<PlatformRig::ModelVisCache> cache)
    {
        _object.reset(new std::shared_ptr<PlatformRig::VisMouseOver>(std::move(attached)));
        _modelSettings.reset(new std::shared_ptr<PlatformRig::ModelVisSettings>(std::move(settings)));
        _modelCache.reset(new std::shared_ptr<PlatformRig::ModelVisCache>(std::move(cache)));
    }

    VisMouseOver::~VisMouseOver() 
    { 
        _object.reset(); 
        _modelSettings.reset(); 
        _modelCache.reset(); 
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template<typename NameType, typename ValueType>
        NameType BindingUtil::PropertyPair<NameType, ValueType>::Name::get() { return _name; }

    template<typename NameType, typename ValueType>
        void BindingUtil::PropertyPair<NameType, ValueType>::Name::set(NameType newValue)
    {
        _name = newValue;
        NotifyPropertyChanged("Name");
    }

    template<typename NameType, typename ValueType>
        ValueType BindingUtil::PropertyPair<NameType, ValueType>::Value::get() { return _value; } 

    template<typename NameType, typename ValueType>
        void BindingUtil::PropertyPair<NameType, ValueType>::Value::set(ValueType newValue)
    {
        _value = newValue;
        NotifyPropertyChanged("Value");
    }

    template<typename NameType, typename ValueType>
        void BindingUtil::PropertyPair<NameType, ValueType>::NotifyPropertyChanged(System::String^ propertyName)
    {
        PropertyChanged(this, gcnew PropertyChangedEventArgs(propertyName));
        // _propertyChangedContext->Send(
        //     gcnew System::Threading::SendOrPostCallback(
        //         o => PropertyChanged(this, gcnew PropertyChangedEventArgs(propertyName))
        //     ), nullptr);
    }

    public ref class BindingConv
    {
    public:
        static BindingList<BindingUtil::StringStringPair^>^ AsBindingList(const ParameterBox& paramBox);
        static ParameterBox AsParameterBox(BindingList<BindingUtil::StringStringPair^>^);
        static ParameterBox AsParameterBox(BindingList<BindingUtil::StringIntPair^>^);

        static BindingList<BindingUtil::StringStringPair^>^ AsBindingList(const RenderCore::Assets::ResourceBindingSet& bindingSet);
        static RenderCore::Assets::ResourceBindingSet AsResourceBindingList(BindingList<BindingUtil::StringStringPair^>^);
    };

    BindingList<BindingUtil::StringStringPair^>^ BindingConv::AsBindingList(const ParameterBox& paramBox)
    {
        auto result = gcnew BindingList<BindingUtil::StringStringPair^>();
        std::vector<std::pair<const char*, std::string>> stringTable;
        paramBox.BuildStringTable(stringTable);

        for (auto i=stringTable.cbegin(); i!=stringTable.cend(); ++i) {
            result->Add(
                gcnew BindingUtil::StringStringPair(
                    clix::marshalString<clix::E_UTF8>(i->first),
                    clix::marshalString<clix::E_UTF8>(i->second)));
        }

        return result;
    }

    ParameterBox BindingConv::AsParameterBox(BindingList<BindingUtil::StringStringPair^>^ input)
    {
        ParameterBox result;
        for each(auto i in input) {
                //  We get items with null names when they are being added, but
                //  not quite finished yet. We have to ignore in this case.
            if (i->Name && i->Name->Length > 0) {
                result.SetParameter(
                    clix::marshalString<clix::E_UTF8>(i->Name).c_str(),
                    clix::marshalString<clix::E_UTF8>(i->Value).c_str());
            }
        }
        return result;
    }

    ParameterBox BindingConv::AsParameterBox(BindingList<BindingUtil::StringIntPair^>^ input)
    {
        ParameterBox result;
        for each(auto i in input) {
                //  We get items with null names when they are being added, but
                //  not quite finished yet. We have to ignore in this case.
            if (i->Name && i->Name->Length > 0) {
                result.SetParameter(
                    clix::marshalString<clix::E_UTF8>(i->Name).c_str(),
                    i->Value);
            }
        }
        return result;
    }

    BindingList<BindingUtil::StringStringPair^>^ BindingConv::AsBindingList(
        const RenderCore::Assets::ResourceBindingSet& bindingSet)
    {
        auto result = gcnew BindingList<BindingUtil::StringStringPair^>();

        for (auto i=bindingSet.cbegin(); i!=bindingSet.cend(); ++i) {
            StringMeld<64> nameTemp;
            nameTemp << std::hex << i->_bindHash;

            result->Add(
                gcnew BindingUtil::StringStringPair(
                    clix::marshalString<clix::E_UTF8>((const char*)nameTemp),
                    clix::marshalString<clix::E_UTF8>(i->_resourceName)));
        }

        return result;
    }

    RenderCore::Assets::ResourceBindingSet BindingConv::AsResourceBindingList(BindingList<BindingUtil::StringStringPair^>^ input)
    {
        using namespace RenderCore::Assets;
        ResourceBindingSet result;

        for each(auto i in input) {
            if (i->Name && i->Name->Length > 0) {
                MaterialGuid guid = XlAtoI64(clix::marshalString<clix::E_UTF8>(i->Name).c_str(), nullptr, 16);
                auto ins = std::lower_bound(
                    result.begin(), result.end(), 
                    guid, ResourceBinding::Compare());
                if (ins==result.end() || ins->_bindHash != guid) {
                    std::string value;
                    if (i->Value) { value = clix::marshalString<clix::E_UTF8>(i->Value); }
                    result.insert(ins, 
                        ResourceBinding(guid, value));
                }
            }
        }

        return result;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    BindingList<BindingUtil::StringStringPair^>^ 
        RawMaterial::MaterialParameterBox::get()
    {
        if (!_underlying.get()) { return nullptr; }
        if (!_materialParameterBox) {
            _materialParameterBox = BindingConv::AsBindingList((*_underlying)->GetAsset()._matParamBox);
            _materialParameterBox->ListChanged += 
                gcnew ListChangedEventHandler(
                    this, &RawMaterial::ParameterBox_Changed);
            _materialParameterBox->AllowNew = true;
            _materialParameterBox->AllowEdit = true;
        }
        return _materialParameterBox;
    }

    BindingList<BindingUtil::StringStringPair^>^ 
        RawMaterial::ShaderConstants::get()
    {
        if (!_underlying.get()) { return nullptr; }
        if (!_shaderConstants) {
            _shaderConstants = BindingConv::AsBindingList((*_underlying)->GetAsset()._constants);
            _shaderConstants->ListChanged += 
                gcnew ListChangedEventHandler(
                    this, &RawMaterial::ParameterBox_Changed);
            _shaderConstants->AllowNew = true;
            _shaderConstants->AllowEdit = true;
        }
        return _shaderConstants;
    }

    BindingList<BindingUtil::StringStringPair^>^ 
        RawMaterial::ResourceBindings::get()
    {
        if (!_underlying.get()) { return nullptr; }
        if (!_resourceBindings) {
            _resourceBindings = BindingConv::AsBindingList((*_underlying)->GetAsset()._resourceBindings);
            _resourceBindings->ListChanged += 
                gcnew ListChangedEventHandler(
                    this, &RawMaterial::ResourceBinding_Changed);
            _resourceBindings->AllowNew = true;
            _resourceBindings->AllowEdit = true;
        }
        return _resourceBindings;
    }

    void RawMaterial::ParameterBox_Changed(System::Object^ obj, ListChangedEventArgs^e)
    {
            //  Commit these changes back to the native object by re-creating the parameter box
            //  Ignore a couple of cases... 
            //      - moving an item is unimportant
            //      - added a new item with a null name (this occurs when the new item
            //          hasn't been fully filled in yet)
            //   Similarly, don't we really need to process a removal of an item with 
            //   an empty name.. but there's no way to detect this case
        if (e->ListChangedType == ListChangedType::ItemMoved) {
            return;
        }

        using Box = BindingList<BindingUtil::StringStringPair^>;

        if (e->ListChangedType == ListChangedType::ItemAdded) {
            assert(e->NewIndex < ((Box^)obj)->Count);
            if (!((Box^)obj)[e->NewIndex]->Name || ((Box^)obj)[e->NewIndex]->Name->Length > 0) {
                return;
            }
        }

        if (_underlying.get()) {
            if (obj == _materialParameterBox) {
                auto transaction = (*_underlying)->Transaction_Begin("Material parameter");
                if (transaction)
                    transaction->GetAsset()._matParamBox = BindingConv::AsParameterBox(_materialParameterBox);
            } else if (obj == _shaderConstants) {
                auto transaction = (*_underlying)->Transaction_Begin("Material constant");
                if (transaction)
                    transaction->GetAsset()._constants = BindingConv::AsParameterBox(_shaderConstants);
            }
        }
    }

    void RawMaterial::ResourceBinding_Changed(System::Object^ obj, ListChangedEventArgs^ e)
    {
        if (e->ListChangedType == ListChangedType::ItemMoved) {
            return;
        }

        using Box = BindingList<BindingUtil::StringStringPair^>;

        if (e->ListChangedType == ListChangedType::ItemAdded) {
            assert(e->NewIndex < ((Box^)obj)->Count);
            if (!((Box^)obj)[e->NewIndex]->Name || ((Box^)obj)[e->NewIndex]->Name->Length > 0) {
                return;
            }
        }

        if (_underlying.get()) {
            assert(obj == _resourceBindings);
            auto transaction = (*_underlying)->Transaction_Begin("Resource Binding");
            if (transaction)
                transaction->GetAsset()._resourceBindings = BindingConv::AsResourceBindingList(_resourceBindings);
        }
    }

    System::Collections::Generic::List<RawMaterial^>^ RawMaterial::BuildInheritanceList()
    {
            // create a RawMaterial wrapper object for all of the inheritted objects
        if (_underlying.get()) {
            auto result = gcnew System::Collections::Generic::List<RawMaterial^>();

            auto& asset = (*_underlying)->GetAsset();
            for (auto i = asset._inherit.cbegin(); i != asset._inherit.cend(); ++i) {
                result->Add(gcnew RawMaterial(
                    clix::marshalString<clix::E_UTF8>(*i)));
            }
            return result;
        }
        return nullptr;
    }

    System::String^ RawMaterial::Filename::get()
    {
        if (!_underlying.get()) { return DummyFilename; }
        return clix::marshalString<clix::E_UTF8>((*_underlying)->GetAsset().GetInitializerFilename());
    }

    System::String^ RawMaterial::SettingName::get()
    {
        if (!_underlying.get()) { return DummySettingName; }
        return clix::marshalString<clix::E_UTF8>((*_underlying)->GetAsset().GetSettingName());
    }

    RawMaterial::RawMaterial(System::String^ initialiser)
    {
        TRY {
            auto nativeInit = clix::marshalString<clix::E_UTF8>(initialiser);
            auto& source = ::Assets::GetDivergentAsset<RenderCore::Assets::RawMaterial>(nativeInit.c_str());
            _underlying.reset(
                new std::shared_ptr<NativeConfig>(source));
            _renderStateSet = gcnew RenderStateSet(*_underlying);
        } CATCH (const Assets::Exceptions::InvalidResource&) {
            auto colon = initialiser->IndexOf(':');
            if (colon > 1) {
                DummyFilename = initialiser->Substring(0, colon);
                DummySettingName = initialiser->Substring(colon+1);
            } else {
                DummyFilename = initialiser;
            }
        } CATCH_END
    }

    RawMaterial::RawMaterial(
        std::shared_ptr<NativeConfig> underlying)
    {
        _underlying.reset(
            new std::shared_ptr<NativeConfig>(std::move(underlying)));
        _renderStateSet = gcnew RenderStateSet(*_underlying);
    }

    RawMaterial::~RawMaterial()
    {
        _underlying.reset();
        delete _renderStateSet;
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    auto RenderStateSet::DoubleSided::get() -> CheckState
    {
        auto& stateSet = (*_underlying)->GetAsset()._stateSet;
        if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::DoubleSided) {
            if (stateSet._doubleSided) return CheckState::Checked;
            else return CheckState::Unchecked;
        }
        return CheckState::Indeterminate;
    }
    
    void RenderStateSet::DoubleSided::set(CheckState checkState)
    {
        auto transaction = (*_underlying)->Transaction_Begin("RenderState");
        auto& stateSet = transaction->GetAsset()._stateSet;
        if (checkState == CheckState::Indeterminate) {
            stateSet._flag &= ~RenderCore::Assets::RenderStateSet::Flag::DoubleSided;
        } else {
            stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::DoubleSided;
            stateSet._doubleSided = (checkState == CheckState::Checked);
        }
        NotifyPropertyChanged("DoubleSided");
    }

    CheckState RenderStateSet::Wireframe::get()
    {
        auto& stateSet = (*_underlying)->GetAsset()._stateSet;
        if (stateSet._flag & RenderCore::Assets::RenderStateSet::Flag::Wireframe) {
            if (stateSet._wireframe) return CheckState::Checked;
            else return CheckState::Unchecked;
        }
        return CheckState::Indeterminate;
    }

    void RenderStateSet::Wireframe::set(CheckState checkState)
    {
        auto transaction = (*_underlying)->Transaction_Begin("RenderState");
        auto& stateSet = transaction->GetAsset()._stateSet;
        if (checkState == CheckState::Indeterminate) {
            stateSet._flag &= ~RenderCore::Assets::RenderStateSet::Flag::Wireframe;
        } else {
            stateSet._flag |= RenderCore::Assets::RenderStateSet::Flag::Wireframe;
            stateSet._wireframe = (checkState == CheckState::Checked);
        }
        NotifyPropertyChanged("Wireframe");
    }

    auto RenderStateSet::DeferredBlend::get() -> DeferredBlendState
    {
        return DeferredBlendState::Unset;
    }
    
    void RenderStateSet::DeferredBlend::set(DeferredBlendState)
    {
        NotifyPropertyChanged("DeferredBlend");
    }

    RenderStateSet::RenderStateSet(std::shared_ptr<::Assets::DivergentAsset<RenderCore::Assets::RawMaterial>> underlying)
    {
        _underlying.reset(
            new std::shared_ptr<::Assets::DivergentAsset<RenderCore::Assets::RawMaterial>>(std::move(underlying)));
        _propertyChangedContext = System::Threading::SynchronizationContext::Current;
    }

    RenderStateSet::~RenderStateSet()
    {
        _underlying.reset();
    }

    void RenderStateSet::NotifyPropertyChanged(System::String^ propertyName)
    {
        PropertyChanged(this, gcnew PropertyChangedEventArgs(propertyName));
        // _propertyChangedContext->Send(
        //     gcnew System::Threading::SendOrPostCallback(
        //         o => PropertyChanged(this, gcnew PropertyChangedEventArgs(propertyName))
        //     ), nullptr);
    }

///////////////////////////////////////////////////////////////////////////////////////////////////

    template BindingUtil::PropertyPair<System::String^, unsigned>;
    template BindingUtil::PropertyPair<System::String^, System::String^>;
}


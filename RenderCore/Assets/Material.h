// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Metal/Forward.h"       // for Metal::Blend
#include "../../Assets/Assets.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/Mixins.h"


namespace Assets { class DependencyValidation; class DirectorySearchRules; }
namespace Utility { class Data; }

namespace RenderCore { namespace Assets
{
    #pragma pack(push)
    #pragma pack(1)

    /// <summary>Render state settings</summary>
    /// These settings are used to select the low-level graphics API render 
    /// state while rendering using this material.
    ///
    /// There are only a few low-level states that are practical & meaningful
    /// to set this way. Often we get fighting between different parts of the
    /// engine wanting to control render states. For example, a graphics effect
    /// may want to select the back face culling mode -- but the material may
    /// have a setting for that as well. So who wins? The material or the 
    /// graphics effect? The answer changes from situation to situation.
    ///
    /// These are difficult problems! To try to avoid, we should make sure that
    /// the material only has settings for the minimal set of states it really 
    /// needs (and free everything else up for higher level stuff)
    ///
    /// RasterizerDesc:
    /// -----------------------------------------------------------------------------
    ///     double-sided culling enable/disable
    ///         Winding direction and CULL_FRONT/CULL_BACK don't really belong here.
    ///         Winding direction should be a property of the geometry and any
    ///         transforms applied to it. And we should only need to select CULL_FRONT
    ///         for special graphics techniques -- they can do it another way
    ///
    ///     depth bias
    ///         Sometimes it's handy to apply some bias at a material level. But it
    ///         should blend somehow with depth bias applied as part of the shadow 
    ///         rendering pass.
    ///
    ///     fill mode
    ///         it's rare to want to change the fill mode. But it feels like it should
    ///         be a material setting (though, I guess it could alternatively be
    ///         attached to the geometry).
    ///
    /// BlendDesc:
    /// -----------------------------------------------------------------------------
    ///     blend mode settings
    ///         This is mostly meaningful during forward rendering operations. But it
    ///         may be handy for deferred decals to select a blend mode at a material
    ///         based level. 
    ///
    ///         There may be some cases where we want to apply different blend mode 
    ///         settings in deferred and forward rendering. That suggests having 2
    ///         separate states -- one for deferred, one for forward.
    ///         We don't really want to use the low-level states in the deferred case,
    ///         because it may depend on the structure of the gbuffer (which is defined
    ///         elsewhere)
    ///
    ///         The blend mode might depend on the texture, as well. If the texture is
    ///         premultiplied alpha, it might end up with a different blend mode than
    ///         when using a non-premultiplied alpha texture.
    ///
    ///         The alpha channel blend settings (and IndependentBlendEnable setting)
    ///         are not exposed.
    ///
    ///     write mask
    ///         It's rare to want to change the write mask -- but it can be an interesting
    ///         trick. It doesn't hurt much to have some behaviour for it here.
    ///
    /// Other possibilities
    /// -----------------------------------------------------------------------------
    ///     stencil write states & stencil test states
    ///         there may be some cases where we want the material to define how we 
    ///         read and write the stencil buffer. Mostly some higher level state will
    ///         control this, but the material may want to have some effect..?
    ///
    /// Also note that alpha test is handled in a different way. We use shader behaviour
    /// (not a render state) to enable/disable 
    class RenderStateSet
    {
    public:
        struct DeferredBlend
        {
            enum Enum { Opaque, Decal };
        };
        unsigned                _doubleSided : 1;
        unsigned                _wireframe : 1;
        unsigned                _writeMask : 4;
        DeferredBlend::Enum     _deferredBlend : 2;

            //  These "blend" values may not be completely portable across all platforms
            //  (either because blend modes aren't supported, or because we need to
            //  change the meaning of the values)
        Metal::Blend::Enum      _forwardBlendSrc : 5;
        Metal::Blend::Enum      _forwardBlendDst : 5;
        Metal::BlendOp::Enum    _forwardBlendOp  : 5;

        struct Flag
        {
            enum Enum {
                DoubleSided = 1<<0, Wireframe = 1<<1, WriteMask = 1<<2, 
                DeferredBlend = 1<<3, ForwardBlend = 1<<4, DepthBias = 1<<5 
            };
            typedef unsigned BitField;
        };
        Flag::BitField          _flag : 6;
        unsigned                _padding : 3;   // 8 + 15 + 32 + 5 = 60 bits... pad to 64 bits

        int                     _depthBias;     // do we need all of the bits for this?

        uint64 GetHash() const;
        RenderStateSet();
    };

    typedef uint64 MaterialGuid;

    class ResourceBinding
    {
    public:
        uint64          _bindHash;
        std::string     _resourceName;

        ResourceBinding(uint64 bindHash, const std::string& resourceName)
            : _bindHash(bindHash), _resourceName(resourceName) {}
        void Serialize(Serialization::NascentBlockSerializer& serializer) const;

        class Compare
        {
        public:
            bool operator()(const ResourceBinding& lhs, uint64 rhs)
            {
                return lhs._bindHash < rhs;
            }
            bool operator()(uint64 lhs, const ResourceBinding& rhs)
            {
                return lhs < rhs._bindHash;
            }
            bool operator()(const ResourceBinding& lhs, 
                            const ResourceBinding& rhs)
            {
                return lhs._bindHash < rhs._bindHash;
            }
        };
    };
    typedef Serialization::Vector<ResourceBinding> ResourceBindingSet;

    /// <summary>Final material settings</summary>
    /// These are some material parameters that can be attached to a 
    /// ModelRunTime. This is the "resolved" format. Material settings
    /// start out as a hierarchical set of parameters. During preprocessing
    /// they should be resolved down to a final flattened form -- which is
    /// this form.
    ///
    /// This is used during runtime to construct the inputs for shaders.
    ///
    /// Material parameters and settings are purposefully kept fairly
    /// low-level. These parameters are settings that can be used during
    /// the main render step (rather than some higher level, CPU-side
    /// operation).
    ///
    /// Typically, material parameters effect these shader inputs:
    ///     Resource bindings (ie, textures assigned to shader resource slots)
    ///     Shader constants
    ///     Some state settings (like blend modes, etc)
    ///
    /// Material parameters can also effect the shader selection through the 
    /// _matParams resource box.
    class ResolvedMaterial : noncopyable
    {
    public:
        ResourceBindingSet _bindings;
        ParameterBox _matParams;
        RenderStateSet _stateSet;
        ParameterBox _constants;

        void Serialize(Serialization::NascentBlockSerializer& serializer) const;

        ResolvedMaterial();
        ResolvedMaterial(ResolvedMaterial&& moveFrom);
        ResolvedMaterial& operator=(ResolvedMaterial&& moveFrom);
    };

    #pragma pack(pop)

    /// <summary>Pre-resolved material settings</summary>
    /// Materials are a hierachical set of properties. Each RawMaterial
    /// object can inherit from sub RawMaterials -- and it can either
    /// inherit or override the properties in those sub RawMaterials.
    ///
    /// RawMaterials are intended to be used in tools (for preprocessing 
    /// and material authoring). ResolvedMaterial is the run-time representation.
    ///
    /// During preprocessing, RawMaterials should be resolved down to a 
    /// ResolvedMaterial object (using the Resolve() method). 
    class RawMaterial
    {
    public:
        ResourceBindingSet _resourceBindings;
        ParameterBox _matParamBox;
        Assets::RenderStateSet _stateSet;
        ParameterBox _constants;

        using ResString = std::basic_string<::Assets::ResChar>;
        std::vector<ResString> _inherit;

        const ::Assets::DependencyValidation& GetDependencyValidation() const { return *_depVal; }

        ResolvedMaterial Resolve(
            const ::Assets::DirectorySearchRules& searchRules,
            std::vector<::Assets::FileAndTime>* deps = nullptr) const;

        std::unique_ptr<Data> SerializeAsData() const;
        ResString GetInitializerFilename() const    { return _splitName._initializerFilename; }
        ResString GetSettingName() const            { return _splitName._settingName; }
        
        RawMaterial();
        RawMaterial(const ::Assets::ResChar initialiser[]);
        RawMaterial(const Data&);
        ~RawMaterial();
    private:
        std::shared_ptr<::Assets::DependencyValidation> _depVal;

        void MergeInto(ResolvedMaterial& dest) const;

        class RawMatSplitName
        {
        public:
            ResString _settingName;
            ResString _concreteFilename;
            ResString _initializerFilename;

            RawMatSplitName();
            RawMatSplitName(const ::Assets::ResChar initialiser[]);
        };
        RawMatSplitName _splitName;
    };

    void MakeConcreteRawMaterialFilename(
        ::Assets::ResChar dest[], unsigned dstCount, const ::Assets::ResChar inputName[]);
    uint64 MakeMaterialGuid(const char name[]);

}}


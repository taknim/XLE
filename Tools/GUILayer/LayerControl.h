// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "EngineControl.h"

namespace GUILayer 
{
    class LayerControlPimpl;

    ref class ModelVisSettings;
    ref class IOverlaySystem;
    ref class VisCameraSettings;
    ref class VisMouseOver;

    public ref class LayerControl : public EngineControl
    {
    public:
        void SetupDefaultVis(ModelVisSettings^ settings, VisMouseOver^ mouseOver);
        VisMouseOver^ CreateVisMouseOver(ModelVisSettings^ settings);

        void AddDefaultCameraHandler(VisCameraSettings^);
        void AddSystem(IOverlaySystem^ overlay);

        LayerControl(Control^ control);
        ~LayerControl();
    protected:
        clix::auto_ptr<LayerControlPimpl> _pimpl;

        virtual void Render(RenderCore::IThreadContext&, IWindowRig&) override;
    };
}



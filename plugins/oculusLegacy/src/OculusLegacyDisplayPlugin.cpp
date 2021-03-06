//
//  Created by Bradley Austin Davis on 2014/04/13.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//
#include "OculusLegacyDisplayPlugin.h"

#include <memory>

#include <QtCore/QThread.h>
#include <QtWidgets/QMainWindow>
#include <QtOpenGL/QGLWidget>
#include <GLMHelpers.h>
#include <QEvent>
#include <QtGui/QResizeEvent>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <gl/GLWindow.h>
#include <gl/GLWidget.h>
#include <MainWindow.h>

#include <gl/QOpenGLContextWrapper.h>
#include <PerfStat.h>
#include <gl/OglplusHelpers.h>
#include <ViewFrustum.h>

#include "plugins/PluginContainer.h"
#include "OculusHelpers.h"

using namespace oglplus;

const QString OculusLegacyDisplayPlugin::NAME("Oculus Rift (0.5) (Legacy)");

OculusLegacyDisplayPlugin::OculusLegacyDisplayPlugin() {
}

void OculusLegacyDisplayPlugin::resetSensors() {
    ovrHmd_RecenterPose(_hmd);
}

void OculusLegacyDisplayPlugin::beginFrameRender(uint32_t frameIndex) {
    _currentRenderFrameInfo = FrameInfo();
    _currentRenderFrameInfo.predictedDisplayTime = _currentRenderFrameInfo.sensorSampleTime = ovr_GetTimeInSeconds();
    _trackingState = ovrHmd_GetTrackingState(_hmd, _currentRenderFrameInfo.predictedDisplayTime);
    _currentRenderFrameInfo.rawRenderPose = _currentRenderFrameInfo.renderPose = toGlm(_trackingState.HeadPose.ThePose);
    Lock lock(_mutex);
    _frameInfos[frameIndex] = _currentRenderFrameInfo;
}

bool OculusLegacyDisplayPlugin::isSupported() const {
    if (!ovr_Initialize(nullptr)) {
        return false;
    }
    bool result = false;
    if (ovrHmd_Detect() > 0) {
        result = true;
    }

    auto hmd = ovrHmd_Create(0);
    if (hmd) {
        QPoint targetPosition{ hmd->WindowsPos.x, hmd->WindowsPos.y };
        auto screens = qApp->screens();
        for(int i = 0; i < screens.size(); ++i) {
            auto screen = screens[i];
            QPoint position = screen->geometry().topLeft();
            if (position == targetPosition) {
                _hmdScreen = i;
                break;
            }
        }
    }
  
    ovr_Shutdown();
    return result;
}

bool OculusLegacyDisplayPlugin::internalActivate() {
    if (!Parent::internalActivate()) {
        return false;
    }

    if (!(ovr_Initialize(nullptr))) {
        Q_ASSERT(false);
        qFatal("Failed to Initialize SDK");
        return false;
    }
    
    
    _hmdWindow = new GLWindow();
    _hmdWindow->create();
    _hmdWindow->createContext(_container->getPrimaryContext());
    auto hmdScreen = qApp->screens()[_hmdScreen];
    auto hmdGeometry = hmdScreen->geometry();
    _hmdWindow->setGeometry(hmdGeometry);
    _hmdWindow->showFullScreen();
  
    _hmdWindow->makeCurrent();
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    _hmdWindow->swapBuffers();
    
    _container->makeRenderingContextCurrent();
    
    QOpenGLContextWrapper(_hmdWindow->context()).moveToThread(_presentThread);

    _hswDismissed = false;
    _hmd = ovrHmd_Create(0);
    if (!_hmd) {
        qFatal("Failed to acquire HMD");
        return false;
    }
    
    _ipd = ovrHmd_GetFloat(_hmd, OVR_KEY_IPD, _ipd);

    glm::uvec2 eyeSizes[2];
    ovr_for_each_eye([&](ovrEyeType eye) {
        _eyeFovs[eye] = _hmd->MaxEyeFov[eye];
        ovrEyeRenderDesc erd = _eyeRenderDescs[eye] = ovrHmd_GetRenderDesc(_hmd, eye, _eyeFovs[eye]);
        ovrMatrix4f ovrPerspectiveProjection =
            ovrMatrix4f_Projection(erd.Fov, DEFAULT_NEAR_CLIP, DEFAULT_FAR_CLIP, ovrProjection_RightHanded);
        _eyeProjections[eye] = toGlm(ovrPerspectiveProjection);
        _ovrEyeOffsets[eye] = erd.HmdToEyeViewOffset;
        _eyeOffsets[eye] = glm::translate(mat4(), -1.0f * toGlm(_ovrEyeOffsets[eye]));
        eyeSizes[eye] = toGlm(ovrHmd_GetFovTextureSize(_hmd, eye, erd.Fov, 1.0f));
    });
    
    auto combinedFov = _eyeFovs[0];
    combinedFov.LeftTan = combinedFov.RightTan = std::max(combinedFov.LeftTan, combinedFov.RightTan);
    _cullingProjection = toGlm(ovrMatrix4f_Projection(combinedFov, DEFAULT_NEAR_CLIP, DEFAULT_FAR_CLIP, ovrProjection_RightHanded));

    _renderTargetSize = uvec2(
        eyeSizes[0].x + eyeSizes[1].x,
        std::max(eyeSizes[0].y, eyeSizes[1].y));
    
    if (!ovrHmd_ConfigureTracking(_hmd,
                                  ovrTrackingCap_Orientation | ovrTrackingCap_Position | ovrTrackingCap_MagYawCorrection, 0)) {
        qFatal("Could not attach to sensor device");
    }

    return true;
}

void OculusLegacyDisplayPlugin::internalDeactivate() {
	Parent::internalDeactivate();
    ovrHmd_Destroy(_hmd);
    _hmd = nullptr;
    ovr_Shutdown();
    _hmdWindow->showNormal();
    _hmdWindow->destroy();
    _hmdWindow->deleteLater();
    _hmdWindow = nullptr;
    _container->makeRenderingContextCurrent();
}

// DLL based display plugins MUST initialize GLEW inside the DLL code.
void OculusLegacyDisplayPlugin::customizeContext() {
    static std::once_flag once;
    std::call_once(once, []{
        glewExperimental = true;
        glewInit();
        glGetError();
    });
    _hmdWindow->requestActivate();
    QThread::msleep(1000);
    Parent::customizeContext();
    ovrGLConfig config; memset(&config, 0, sizeof(ovrRenderAPIConfig));
    auto& header = config.Config.Header;
    header.API = ovrRenderAPI_OpenGL;
    header.BackBufferSize = _hmd->Resolution;
    header.Multisample = 1;
    int distortionCaps = ovrDistortionCap_TimeWarp | ovrDistortionCap_Vignette;
    
    memset(_eyeTextures, 0, sizeof(ovrTexture) * 2);
    ovr_for_each_eye([&](ovrEyeType eye) {
        auto& header = _eyeTextures[eye].Header;
        header.API = ovrRenderAPI_OpenGL;
        header.TextureSize = { (int)_renderTargetSize.x, (int)_renderTargetSize.y };
        header.RenderViewport.Size = header.TextureSize;
        header.RenderViewport.Size.w /= 2;
        if (eye == ovrEye_Right) {
            header.RenderViewport.Pos.x = header.RenderViewport.Size.w;
        }
    });
    
    if (_hmdWindow->makeCurrent()) {
#ifndef NDEBUG
        ovrBool result =
#endif
        ovrHmd_ConfigureRendering(_hmd, &config.Config, distortionCaps, _eyeFovs, _eyeRenderDescs);
        assert(result);
        _hmdWindow->doneCurrent();
    }

    
}

void OculusLegacyDisplayPlugin::uncustomizeContext() {
    _hmdWindow->doneCurrent();
    QOpenGLContextWrapper(_hmdWindow->context()).moveToThread(qApp->thread());
    Parent::uncustomizeContext();
}

void OculusLegacyDisplayPlugin::hmdPresent() {
    if (!_hswDismissed) {
        ovrHSWDisplayState hswState;
        ovrHmd_GetHSWDisplayState(_hmd, &hswState);
        if (hswState.Displayed) {
            ovrHmd_DismissHSWDisplay(_hmd);
        }

    }
    auto r = glm::quat_cast(_currentPresentFrameInfo.presentPose);
    ovrQuatf ovrRotation = { r.x, r.y, r.z, r.w };
    ovrPosef eyePoses[2];
    memset(eyePoses, 0, sizeof(ovrPosef) * 2);
    eyePoses[0].Orientation = eyePoses[1].Orientation = ovrRotation;
    
    GLint texture = oglplus::GetName(_compositeFramebuffer->color);
    auto sync = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    if (_hmdWindow->makeCurrent()) {
        glClearColor(0, 0.4, 0.8, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ovrHmd_BeginFrame(_hmd, _currentPresentFrameIndex);
        glWaitSync(sync, 0, GL_TIMEOUT_IGNORED);
        glDeleteSync(sync);
        ovr_for_each_eye([&](ovrEyeType eye) {
            reinterpret_cast<ovrGLTexture&>(_eyeTextures[eye]).OGL.TexId = texture;
        });
        ovrHmd_EndFrame(_hmd, eyePoses, _eyeTextures);
        _hmdWindow->doneCurrent();
    }
    static auto widget = _container->getPrimaryWidget();
    widget->makeCurrent();
}

int OculusLegacyDisplayPlugin::getHmdScreen() const {
    return _hmdScreen;
}

float OculusLegacyDisplayPlugin::getTargetFrameRate() const {
    return TARGET_RATE_OculusLegacy;
}


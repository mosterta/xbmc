/*
 *  Copyright (C) 2017-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/VideoPlayer/VideoRenderers/BaseRenderer.h"
#include "cores/VideoPlayer/VideoRenderers/HwDecRender/VdpauCedar.h"
#include "cores/VideoPlayer/DVDCodecs/Video/VDPAU.h"

//class CVdpauRenderPicture;

class CRendererVDPAUSunxi
  : public CBaseRenderer
{
public:
  CRendererVDPAUSunxi();
  ~CRendererVDPAUSunxi();

  // Registration
  static CBaseRenderer* Create(CVideoBuffer* buffer);
  static void Register();

  // Player functions
  bool Configure(const VideoPicture& picture, float fps, unsigned int orientation) override;
  bool IsConfigured() override { return m_bConfigured; };
  void AddVideoPicture(const VideoPicture& picture, int index) override;
  void UnInit() override {};
  bool Flush(bool saveBuffers) override;
  void ReleaseBuffer(int idx) override;
  bool NeedBuffer(int idx) override;
  bool IsGuiLayer() override { return false; };
  CRenderInfo GetRenderInfo() override;
  void Update() override;
  void RenderUpdate(int index, int index2, bool clear, unsigned int flags, unsigned int alpha) override;
  bool RenderCapture(CRenderCapture* capture) override;
  bool ConfigChanged(const VideoPicture& picture) override;

  // Feature support
  bool SupportsMultiPassRendering() override { return false; };
  bool Supports(ERENDERFEATURE feature) override;
  bool Supports(ESCALINGMETHOD method) override;

protected:
//   void ManageRenderArea() override;

private:
  bool m_bConfigured = false;
  int m_iLastRenderBuffer = -1;

  //std::shared_ptr<CVideoLayerBridgeDRMPRIME> m_videoLayerBridge;
  VDPAU::CInteropStateCedar m_interopState;

  struct BUFFER
  {
    CVideoBuffer* videoBuffer = nullptr;
    int fence = -1;
    VDPAU::CVdpauTextureCedar texture;
  } m_buffers[NUM_BUFFERS];
  
  CHwLayerAdaptorVdpauAllwinner m_vdpauAdaptor;
  unsigned int m_lastRenderTime = 0;

};

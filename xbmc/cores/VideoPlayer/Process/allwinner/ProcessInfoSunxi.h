/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#pragma once

#include "cores/IPlayer.h"
#include "cores/VideoPlayer/Process/ProcessInfo.h"

#include "cores/VideoPlayer/VideoRenderers/HwDecRender/VdpauCedar.h"
#include "vdpau/vdpau.h"

//-----------------------------------------------------------------------------
//
//-----------------------------------------------------------------------------
class CVideoBufferRefSunxi : public CVideoBuffer
{
  public:
    CVideoBufferRefSunxi(AVCodecContext *avctx, VDPAU::InteropInfoCedar &interop, int chromaType, int ycbcrFormat, int width, int height, int id);
    ~CVideoBufferRefSunxi();
    void config(AVCodecContext *avctx, int chromaType, int ycbcrFormat, int width, int height);
    void map(uint8_t *buf[], int linesize[]);
    VDPAU::InteropInfoCedar& getInterop() { return m_interop; };
    AVBufferRef *getBufRef() { m_refCnt++; return m_bufRef; };
    void Unref();
    void createFrame();

  protected:
    static void FFReleaseBuffer(void *opaque, uint8_t *data);

    AVBufferRef* m_bufRef;
    VdpVideoSurface m_surf;
    VDPAU::InteropInfoCedar m_interop;
    int m_linesize[4] = {0};
    int m_chromaType;
    int m_ycbcrFormat;
    int m_width;
    int m_height;
    uint32_t m_refCnt;
};

class CVideoBufferRefPoolSunxi : public IVideoBufferPool
{
  public:
    CVideoBufferRefPoolSunxi(VDPAU::InteropInfoCedar &interop);
    ~CVideoBufferRefPoolSunxi();
    void Return(int id);
    CVideoBufferRefSunxi* Get(AVCodecContext *avctx, int chromaType, int ycbcrFormat, int wight, int height);
    //std::shared_ptr<IRenderBufferPool> GetPtr() { return shared_from_this(); }
    CVideoBufferRefSunxi* Get() override { return nullptr;};

  protected:
    CCriticalSection m_critSection;
    std::vector<CVideoBufferRefSunxi*> m_all;
    std::deque<int> m_used;
    std::deque<int> m_free;
    VDPAU::InteropInfoCedar m_interop;
};

class CVideoBufferSunxi : public CVideoBuffer
{
  public:
    CVideoBufferSunxi(IVideoBufferPool &pool, VDPAU::InteropInfoCedar &interop, int id);
    ~CVideoBufferSunxi() override;
    void GetPlanes(uint8_t*(&planes)[YuvImage::MAX_PLANES]) override;
    void GetStrides(int(&strides)[YuvImage::MAX_PLANES]) override;
    uint8_t* GetMemPtr();

    void SetRef(AVFrame *frame);
    void Unref();

  protected:
    AVFrame* m_pFrame;
    VDPAU::InteropInfoCedar m_interop;
    int chromaType;
    int ycbcrFormat;
    int width;
    int height;
};

class CVideoBufferPoolSunxi : public IVideoBufferPool
{
  public:
    CVideoBufferPoolSunxi(VDPAU::InteropInfoCedar &interop);
    ~CVideoBufferPoolSunxi() override;
    void Return(int id) override;
    CVideoBuffer* Get() override;
    CVideoBufferRefPoolSunxi *GetRefPool() { return m_videoBufferRefPoolSunxi.get(); };
    int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);

  protected:
    CCriticalSection m_critSection;
    std::vector<CVideoBufferSunxi*> m_all;
    std::deque<int> m_used;
    std::deque<int> m_free;
    VDPAU::InteropInfoCedar m_interop;
    std::shared_ptr<CVideoBufferRefPoolSunxi> m_videoBufferRefPoolSunxi;
};

class CVideoBufferManagerSunxi : public CVideoBufferManager
{
public:
  CVideoBufferManagerSunxi();
  void RegisterPool(std::shared_ptr<IVideoBufferPool> pool);
  void ReleasePools();
  CVideoBuffer* Get(AVPixelFormat format, int width, int height);
  CVideoBuffer* Get(AVPixelFormat format, int size);
  void SetDimensions(int width, int height, const int (&strides)[YuvImage::MAX_PLANES]);

protected:
};

class CProcessInfoSunxi : public CProcessInfo
{
public:
  CProcessInfoSunxi();
  static CProcessInfo* Create();
  static void Register();
  EINTERLACEMETHOD GetFallbackDeintMethod() override;
  bool AllowDTSHDDecode() override;

protected:
  VDPAU::CInteropStateCedar m_interopState;

};

/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ProcessInfoSunxi.h"
#include "utils/log.h"

#if not defined (GLsizei)
typedef int GLsizei;
#endif

#include <libcedarDisplay.h>

extern "C" {
#include "libavutil/imgutils.h"
}

// Override for platform ports

CProcessInfo* CProcessInfoSunxi::Create()
{
  return new CProcessInfoSunxi();
}

CProcessInfoSunxi::CProcessInfoSunxi()
{
  m_interopState.Init(0,0,0);
  m_bufferPool = std::make_shared<CVideoBufferPoolSunxi>(m_interopState.GetInterop());
  m_videoBufferManager.RegisterPool(m_bufferPool);
}

void CProcessInfoSunxi::Register()
{
  CProcessInfo::RegisterProcessControl("allwinner", CProcessInfoSunxi::Create);
}

EINTERLACEMETHOD CProcessInfoSunxi::GetFallbackDeintMethod()
{
  return EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BOB;
}

bool CProcessInfoSunxi::AllowDTSHDDecode()
{
  return true;
}

void CProcessInfoSunxi::SetSwDeinterlacingMethods()
{
  // first populate with the defaults from base implementation
  CProcessInfo::SetSwDeinterlacingMethods();

  std::list<EINTERLACEMETHOD> methods;
  {
    // get the current methods
    CSingleLock lock(m_videoCodecSection);
    methods = m_deintMethods;
  }
  // add bob and blend deinterlacer for osx
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BLEND);
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_WEAVE);
  methods.push_back(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BOB);

  UpdateDeinterlacingMethods(methods);
  SetDeinterlacingMethodDefault(EINTERLACEMETHOD::VS_INTERLACEMETHOD_RENDER_BOB);
}

std::shared_ptr<CVideoBufferPoolSunxi> CProcessInfoSunxi::GetBufferPool()
{
  return m_bufferPool;
}

CVideoBufferRefSunxi::CVideoBufferRefSunxi(AVCodecContext *avctx, VDPAU::InteropInfoCedar &interop, int chromaType, 
                                           int ycbcrFormat, int width, int height, int id, int format)
  : CVideoBuffer(id), m_bufRef(nullptr), m_surf(VDP_INVALID_HANDLE), m_interop(interop), m_chromaType(0), 
                 m_ycbcrFormat(0), m_width(0), m_height(0), m_refCnt(0), m_format(format)
{
  config(avctx, chromaType, ycbcrFormat, width, height);
  createBuffer();
}

CVideoBufferRefSunxi::~CVideoBufferRefSunxi()
{
  CLog::Log(LOGDEBUG, " (CVideoBufferRefSunxi) %s, surf:%d", __FUNCTION__, m_surf);
  if (m_surf != VDP_INVALID_HANDLE)
    m_interop.glVDPAUDestroySurfaceCedar(m_surf);

  if(m_bufRef )
  {
    av_buffer_unref(&m_bufRef);
  }
}

void CVideoBufferRefSunxi::config(AVCodecContext *avctx, int chromaType, int ycbcrFormat, int width, int height)
{
  if(m_chromaType != chromaType || m_ycbcrFormat != ycbcrFormat || m_width != width || m_height != height)
  {
    m_chromaType = chromaType;
    m_ycbcrFormat = ycbcrFormat;
    m_width = width;
    m_height = height;

    if(m_surf != VDP_INVALID_HANDLE)
    {
      CLog::Log(LOGDEBUG, "CVideoBufferRefSunxi::%s - (re-)config - Video surface=%d destroyed", __FUNCTION__, m_surf);
      m_interop.glVDPAUDestroySurfaceCedar(m_surf);
    }

    int vdp_st;
    int stride_align[8];
    int unaligned;
    int w = width;
    int h = height;

    avcodec_align_dimensions2(avctx, &w, &h, stride_align);

    if(avctx->pix_fmt != AV_PIX_FMT_VDPAU)
    {
      do {
        // NOTE: do not align linesizes individually, this breaks e.g. assumptions
        // that linesize[0] == 2*linesize[1] in the MPEG-encoder for 4:2:2
        int ret = av_image_fill_linesizes(m_linesize, avctx->pix_fmt, w);
        if (ret < 0)
          return ;
        // increase alignment of w for next try (rhs gives the lowest bit set in w)
        w += w & ~(w - 1);
  
        unaligned = 0;
        for (int i = 0; i < 4; i++)
          unaligned |= m_linesize[i] % stride_align[i];
      } while (unaligned);

      m_surf = 0xdeadbeaf;
    }
    else
      m_surf = 0;

    vdp_st = m_interop.glVDPAUCreateSurfaceCedar(chromaType, ycbcrFormat, width, height, &m_surf);
    if (vdp_st != 0)
    {
      CLog::Log(LOGERROR, "CVideoBufferRefSunxi::%s - No Video surface available could be created", __FUNCTION__);
      return ;
    }
    CLog::Log(LOGDEBUG, " (CVideoBufferRefSunxi) %s created new surface %d", __FUNCTION__, m_surf);
  } 
}

void CVideoBufferRefSunxi::createBuffer()
{
  CLog::Log(LOGDEBUG, "CVideoBufferRefSunxi::%s - create buffer ref surface, surface:%d", __FUNCTION__, m_surf);
  if(!m_bufRef)
    m_bufRef = av_buffer_create((uint8_t*)m_surf, 0, FFReleaseBuffer, this, 0);
  if (!m_bufRef) 
  {
    m_interop.glVDPAUDestroySurfaceCedar(m_surf);
    m_surf = -1;
  }
}

void CVideoBufferRefSunxi::map(uint8_t *buf[], int linesize[])
{
  if(m_format == AV_PIX_FMT_VDPAU)
  {
    buf[0] = buf[3] = (uint8_t*)m_surf;
  }
  else
  {
    m_interop.glVDPAUGetMappedMemoryCedar(m_surf, (void**)&buf[0], (void**)&buf[1], (void**)&buf[2]);
  }
  memcpy(linesize, m_linesize, sizeof(m_linesize));
}

void CVideoBufferRefSunxi::Unref()
{
  av_buffer_unref(&m_bufRef);
}

CVideoBufferRefPoolSunxi::CVideoBufferRefPoolSunxi(VDPAU::InteropInfoCedar &interop) :
    m_interop(interop)
{
}

CVideoBufferRefPoolSunxi::~CVideoBufferRefPoolSunxi()
{
  CLog::Log(LOGDEBUG, " (CVideoBufferRefPoolSunxi) %s", __FUNCTION__ );
  CSingleLock lock(m_critSection);
  for (auto buf : m_all)
  {
    delete buf;
  }
}

CVideoBufferRefSunxi* CVideoBufferRefPoolSunxi::Get(AVCodecContext *avctx, int chromaType, int ycbcrFormat, int width, int height, int format)
{
  CSingleLock lock(m_critSection);

  CVideoBufferRefSunxi *buf = nullptr;
  if (!m_free.empty())
  {
    int idx = m_free.front();
    m_free.pop_front();
    buf = m_all[idx];
    //buf->config(avctx, chromaType, ycbcrFormat, width, height);
    buf->createBuffer();
    m_used.push_back(idx);
    CLog::Log(LOGDEBUG, " (CVideoBufferRefPoolSunxi) %s re-use buffer %d", __FUNCTION__, idx);
  }
  if (buf == NULL)
  {
    int id = m_all.size();
    buf = new CVideoBufferRefSunxi(avctx, m_interop, chromaType, ycbcrFormat, width, height, id, format);
    assert(buf);

    m_all.push_back(buf);
    m_used.push_back(id);
    CLog::Log(LOGDEBUG, " (CVideoBufferRefPoolSunxi) %s create new buffer %d", __FUNCTION__, id);
  }

  buf->Acquire(GetPtr());
  return buf;
}

void CVideoBufferRefPoolSunxi::Return(int id)
{
  CLog::Log(LOGDEBUG, " (CVideoBufferRefPoolSunxi) %s, buffer id:%d", __FUNCTION__, id);
  CSingleLock lock(m_critSection);

  m_all[id]->Unref();
  auto it = m_used.begin();
  while (it != m_used.end())
  {
    if (*it == id)
    {
      m_used.erase(it);
      break;
    }
    else
      ++it;
  }
  m_free.push_back(id);
}

CVideoBufferSunxi::CVideoBufferSunxi(IVideoBufferPool &pool, VDPAU::InteropInfoCedar &interop, int id)
  : CVideoBuffer(id), m_interop(interop)
{
  m_pFrame = av_frame_alloc();
  assert(m_pFrame);
}

CVideoBufferSunxi::~CVideoBufferSunxi()
{
  av_frame_free(&m_pFrame);
}

void CVideoBufferSunxi::GetPlanes(uint8_t*(&planes)[YuvImage::MAX_PLANES])
{
  uint8_t* planeAddr[4];
  if(m_pFrame->format == AV_PIX_FMT_VDPAU)
  {
    m_interop.glVDPAUGetMappedMemoryCedar((VdpVideoSurface)m_pFrame->data[0], (void**)&planeAddr[0], (void**)&planeAddr[1], (void**)&planeAddr[2]);
    planes[0] = planeAddr[0];
    planes[1] = planeAddr[1];
    planes[2] = planeAddr[2];
  }
  else
  {
    planes[0] = m_pFrame->data[0];
    planes[1] = m_pFrame->data[1];
    planes[2] = m_pFrame->data[2];
  }
}

void CVideoBufferSunxi::GetStrides(int(&strides)[YuvImage::MAX_PLANES])
{
  strides[0] = m_pFrame->linesize[0];
  strides[1] = m_pFrame->linesize[1];
  strides[2] = m_pFrame->linesize[2];
}

void CVideoBufferSunxi::SetRef(AVFrame *frame)
{
  av_frame_unref(m_pFrame);
  av_frame_move_ref(m_pFrame, frame);
  m_pixFormat = (AVPixelFormat)m_pFrame->format;
}

void CVideoBufferSunxi::GetRef(AVFrame *frame)
{
  av_frame_move_ref(frame, m_pFrame);
}

void CVideoBufferSunxi::Unref()
{
  av_frame_unref(m_pFrame);
}

uint8_t* CVideoBufferSunxi::GetMemPtr() 
{ 
  return m_pFrame->buf[0]->data; 
};

void CVideoBufferSunxi::SetPictureParams(VideoPicture* pVideoPicture)
{
  pVideoPicture->iWidth = m_pFrame->width;
  pVideoPicture->iHeight = m_pFrame->height;

  double aspect_ratio = 0;
  AVRational pixel_aspect = m_pFrame->sample_aspect_ratio;
  if (pixel_aspect.num)
    aspect_ratio = av_q2d(pixel_aspect) * pVideoPicture->iWidth / pVideoPicture->iHeight;

  if (aspect_ratio <= 0.0)
    aspect_ratio = (float)pVideoPicture->iWidth / (float)pVideoPicture->iHeight;

  pVideoPicture->iDisplayWidth = ((int)lrint(pVideoPicture->iHeight * aspect_ratio)) & -3;
  pVideoPicture->iDisplayHeight = pVideoPicture->iHeight;
  if (pVideoPicture->iDisplayWidth > pVideoPicture->iWidth)
  {
    pVideoPicture->iDisplayWidth = pVideoPicture->iWidth;
    pVideoPicture->iDisplayHeight = ((int)lrint(pVideoPicture->iWidth / aspect_ratio)) & -3;
  }

  pVideoPicture->color_range = m_pFrame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
  pVideoPicture->color_primaries = m_pFrame->color_primaries;
  pVideoPicture->color_transfer = m_pFrame->color_trc;
  pVideoPicture->color_space = m_pFrame->colorspace;

  pVideoPicture->iRepeatPicture = 0;
  pVideoPicture->iFlags = 0;
  pVideoPicture->iFlags |= m_pFrame->interlaced_frame ? DVP_FLAG_INTERLACED : 0;
  pVideoPicture->iFlags |= m_pFrame->top_field_first ? DVP_FLAG_TOP_FIELD_FIRST: 0;
  pVideoPicture->iFlags |= m_pFrame->data[0] ? 0 : DVP_FLAG_DROPPED;

  int64_t pts = m_pFrame->pts;
  if (pts == AV_NOPTS_VALUE)
    pts = m_pFrame->best_effort_timestamp;
  pVideoPicture->pts = (pts == AV_NOPTS_VALUE) ? DVD_NOPTS_VALUE : (double)pts * DVD_TIME_BASE / AV_TIME_BASE;
  pVideoPicture->dts = DVD_NOPTS_VALUE;
}

CVideoBufferPoolSunxi::CVideoBufferPoolSunxi(VDPAU::InteropInfoCedar &interop) :
    m_interop(interop), m_videoBufferRefPoolSunxi(std::make_shared<CVideoBufferRefPoolSunxi>(interop))
{
}

CVideoBufferPoolSunxi::~CVideoBufferPoolSunxi()
{
  CLog::Log(LOGDEBUG, " (CVideoBufferPoolSunxi) %s", __FUNCTION__ );
  CSingleLock lock(m_critSection);
  for (auto buf : m_all)
  {
    delete buf;
  }
  m_videoBufferRefPoolSunxi.reset();
}

CVideoBuffer* CVideoBufferPoolSunxi::Get()
{
  CSingleLock lock(m_critSection);

  CVideoBufferSunxi *buf = nullptr;
  if (!m_free.empty())
  {
    int idx = m_free.front();
    m_free.pop_front();
    m_used.push_back(idx);
    buf = m_all[idx];
    CLog::Log(LOGDEBUG, " (CVideoBufferPoolSunxi) %s, re-use buffer %d", __FUNCTION__ ,idx);
  }
  else
  {
    int id = m_all.size();
    buf = new CVideoBufferSunxi(*this, m_interop, id);
    m_all.push_back(buf);
    m_used.push_back(id);
    CLog::Log(LOGDEBUG, " (CVideoBufferPoolSunxi) %s new buffer %d", __FUNCTION__, id);
  }

  buf->Acquire(GetPtr());
  return buf;
}

void CVideoBufferPoolSunxi::Return(int id)
{
  CLog::Log(LOGDEBUG, " (CVideoBufferPoolSunxi) %s, buffer id:%d", __FUNCTION__, id);
  CSingleLock lock(m_critSection);

  m_all[id]->Unref();
  auto it = m_used.begin();
  while (it != m_used.end())
  {
    if (*it == id)
    {
      m_used.erase(it);
      break;
    }
    else
      ++it;
  }
  m_free.push_back(id);
}

void CVideoBufferPoolSunxi::Configure(AVPixelFormat format, int size)
{
  m_pixFormat = format;
  m_size = size;
  m_configured = true;
}

inline bool CVideoBufferPoolSunxi::IsConfigured()
{
  return m_configured;
}

bool CVideoBufferPoolSunxi::IsCompatible(AVPixelFormat format, int size)
{
  if (m_pixFormat == format &&
      m_size == size)
    return true;

  return false;
}

int CVideoBufferPoolSunxi::FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags)
{
  CSingleLock lock(m_critSection);
  CLog::Log(LOGDEBUG, " (CVideoBufferPoolSunxi) %s", __FUNCTION__);

  CVideoBufferRefPoolSunxi* bufPool(GetRefPool());

  int chromaType;
  int ycbcrFormat;
 
    // create a new surface
  switch (pic->format) {
    case AV_PIX_FMT_VDPAU:
      chromaType = VDP_CHROMA_TYPE_420; 
      ycbcrFormat = VDP_YCBCR_FORMAT_NV12;
      break;
    case AV_PIX_FMT_YUV420P: 
      chromaType = VDP_CHROMA_TYPE_420; 
      ycbcrFormat = VDP_YCBCR_FORMAT_YV12;
      break;
    case AV_PIX_FMT_YUV422P: 
      chromaType = VDP_CHROMA_TYPE_422; 
      ycbcrFormat = VDP_YCBCR_FORMAT_YV12;
      break;
    case AV_PIX_FMT_YUV444P: 
      chromaType = VDP_CHROMA_TYPE_444;
      ycbcrFormat = VDP_YCBCR_FORMAT_YV12;
      break;
    default:
      CLog::Log(LOGERROR, " (FFMPEG) %s: Unsupported data layout: %s", __FUNCTION__,
                av_get_pix_fmt_name((AVPixelFormat)pic->format)
               );
      return -1;
  }

  memset(pic->data, 0, sizeof(pic->data));
  pic->extended_data = pic->data;

  memset(pic->buf, 0, sizeof(pic->buf));
  CVideoBufferRefSunxi *bufRef = bufPool->Get(avctx, chromaType, ycbcrFormat, pic->width, pic->height, pic->format);
  assert(bufRef);
  pic->buf[0] = bufRef->getBufRef();
  if (!pic->buf[0])
    goto fail;

  uint8_t *buf[4];
  int linesize[4];
  bufRef->map(buf, linesize);

  for(int i=0; i < 4; ++i)
  {
    pic->data[i] = (uint8_t*)buf[i];
    pic->linesize[i] = linesize[i];
  }

  //pic->linesize[3] = 0;

  //pic->reordered_opaque= s->reordered_opaque;

  return 0;
fail:
    av_frame_unref(pic);
    return AVERROR(ENOMEM);
}

void CVideoBufferRefSunxi::FFReleaseBuffer(void *opaque, uint8_t *data)
{
  CVideoBufferRefSunxi  *bufRef = (CVideoBufferRefSunxi *)opaque;
  
  bufRef->Release();

  CLog::Log(LOGDEBUG, " (CVideoBufferRefSunxi) %s releasing videobuf id=%d", __FUNCTION__, bufRef->GetId());
}


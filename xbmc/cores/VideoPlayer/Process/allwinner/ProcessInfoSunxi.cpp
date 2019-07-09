/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "ProcessInfoSunxi.h"
#include "utils/log.h"

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
  std::shared_ptr<IVideoBufferPool> pool = std::make_shared<CVideoBufferPoolSunxi>(m_interopState.GetInterop());
  m_videoBufferManager.RegisterPool(pool);
}

void CProcessInfoSunxi::Register()
{
  CProcessInfo::RegisterProcessControl("allwinner", CProcessInfoSunxi::Create);
}

EINTERLACEMETHOD CProcessInfoSunxi::GetFallbackDeintMethod()
{
  return EINTERLACEMETHOD::VS_INTERLACEMETHOD_DEINTERLACE_HALF;
}

bool CProcessInfoSunxi::AllowDTSHDDecode()
{
  return true;
}


CVideoBufferRefSunxi::CVideoBufferRefSunxi(AVCodecContext *avctx, VDPAU::InteropInfoCedar &interop, int chromaType, 
                                           int ycbcrFormat, int width, int height, int id)
  : CVideoBuffer(id), m_bufRef(nullptr), m_surf(-1), m_interop(interop), m_chromaType(0), 
                 m_ycbcrFormat(0), m_width(0), m_height(0), m_refCnt(0)
{
  config(avctx, chromaType, ycbcrFormat, width, height);
  createFrame();
}

CVideoBufferRefSunxi::~CVideoBufferRefSunxi()
{
  if (m_surf != -1)
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

    CLog::Log(LOGERROR, "CVDPAU::FFGetBuffer - reconfig - Video surface=%d destroyed", m_surf);
    m_interop.glVDPAUDestroySurfaceCedar(m_surf);

    int vdp_st;
    int stride_align[8];
    int unaligned;
    int w = width;
    int h = height;

    avcodec_align_dimensions2(avctx, &w, &h, stride_align);

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

    vdp_st = m_interop.glVDPAUCreateSurfaceCedar(chromaType, ycbcrFormat, width, height, &m_surf);
    if (vdp_st != 0)
    {
      CLog::Log(LOGERROR, "CVDPAU::FFGetBuffer - No Video surface available could be created");
      return ;
    }
  } 
}

void CVideoBufferRefSunxi::createFrame()
{
  CLog::Log(LOGERROR, "CVDPAU::FFGetBuffer - Video surface=%d created", m_surf);
    
  if(m_bufRef)
  {
    m_bufRef->data = (uint8_t*)m_surf;
  }
  else 
  {
    m_bufRef = av_buffer_create((uint8_t*)m_surf, 0, FFReleaseBuffer, this, 0);
    if (!m_bufRef) 
    {
      m_interop.glVDPAUDestroySurfaceCedar(m_surf);
      m_surf = -1;
    }
  }
}

void CVideoBufferRefSunxi::map(uint8_t *buf[], int linesize[])
{
  m_interop.glVDPAUGetMappedMemoryCedar(m_surf, (void**)&buf[0], (void**)&buf[1], (void**)&buf[2]);
  memcpy(linesize, m_linesize, sizeof(m_linesize));
}

/*
void CVideoBufferRefSunxi::Acquire()
{
  m_refCnt++;
}
*/
void CVideoBufferRefSunxi::Unref()
{
  m_refCnt--;
}

CVideoBufferRefPoolSunxi::CVideoBufferRefPoolSunxi(VDPAU::InteropInfoCedar &interop) :
    m_interop(interop)
{
}

CVideoBufferRefPoolSunxi::~CVideoBufferRefPoolSunxi()
{
  for (auto buf : m_all)
  {
    delete buf;
  }
}

CVideoBufferRefSunxi* CVideoBufferRefPoolSunxi::Get(AVCodecContext *avctx, int chromaType, int ycbcrFormat, int width, int height)
{
  CSingleLock lock(m_critSection);

  CVideoBufferRefSunxi *buf = nullptr;
  if (!m_free.empty())
  {
    int idx = m_free.front();
    m_free.pop_front();
    buf = m_all[idx];
    buf->config(avctx, chromaType, ycbcrFormat, width, height);
    buf->createFrame();
    m_used.push_back(idx);
  }
  if (buf == NULL)
  {
    int id = m_all.size();
    buf = new CVideoBufferRefSunxi(avctx, m_interop, chromaType, ycbcrFormat, width, height, id);

    m_all.push_back(buf);
    m_used.push_back(id);
  }

  buf->Acquire(GetPtr());
  return buf;
}

void CVideoBufferRefPoolSunxi::Return(int id)
{
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
}

CVideoBufferSunxi::~CVideoBufferSunxi()
{
  av_frame_free(&m_pFrame);
}

void CVideoBufferSunxi::GetPlanes(uint8_t*(&planes)[YuvImage::MAX_PLANES])
{
  planes[0] = m_pFrame->data[0];
  planes[1] = m_pFrame->data[1];
  planes[2] = m_pFrame->data[2];
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

void CVideoBufferSunxi::Unref()
{
  av_frame_unref(m_pFrame);
}

uint8_t* CVideoBufferSunxi::GetMemPtr() 
{ 
  return m_pFrame->buf[0]->data; 
};

CVideoBufferPoolSunxi::CVideoBufferPoolSunxi(VDPAU::InteropInfoCedar &interop) :
    m_interop(interop), m_videoBufferRefPoolSunxi(std::make_shared<CVideoBufferRefPoolSunxi>(interop))
{
}

CVideoBufferPoolSunxi::~CVideoBufferPoolSunxi()
{
  for (auto buf : m_all)
  {
    delete buf;
  }
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
  }
  else
  {
    int id = m_all.size();
    buf = new CVideoBufferSunxi(*this, m_interop, id);
    m_all.push_back(buf);
    m_used.push_back(id);
  }

  buf->Acquire(GetPtr());
  return buf;
}

void CVideoBufferPoolSunxi::Return(int id)
{
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

int CVideoBufferPoolSunxi::FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags)
{
  CLog::Log(LOGDEBUG, " (CVideoBufferRefPoolSunxi) %s", __FUNCTION__);

  CVideoBufferRefPoolSunxi* bufPool(GetRefPool());

  int chromaType;
  int ycbcrFormat;
 
    // create a new surface
  switch (pic->format) {
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
  CVideoBufferRefSunxi *bufRef = bufPool->Get(avctx, chromaType, ycbcrFormat, avctx->coded_width, avctx->coded_height);
  pic->buf[0] = bufRef->getBufRef();
  if (!pic->buf[0])
    goto fail;

  uint8_t *buf[4];
  int linesize[4];
  bufRef->map(buf, linesize);

  for(int i=0; i < 3; ++i)
  {
    pic->data[i] = (uint8_t*)buf[i];
    pic->linesize[i] = linesize[i];
  }

  pic->linesize[3] = 0;

  //pic->reordered_opaque= s->reordered_opaque;

  return 0;
fail:
    av_frame_unref(pic);
    return AVERROR(ENOMEM);
}

void CVideoBufferRefSunxi::FFReleaseBuffer(void *opaque, uint8_t *data)
{
  CLog::Log(LOGNOTICE, " (VDPAU) %s", __FUNCTION__);
  
  CVideoBufferRefSunxi  *bufRef = (CVideoBufferRefSunxi *)opaque;
  
  bufRef->Release();
  bufRef->m_bufRef = NULL;

  CLog::Log(LOGERROR, " (VDPAU) %s releasing videobuf =%d", __FUNCTION__, bufRef->GetId());
}


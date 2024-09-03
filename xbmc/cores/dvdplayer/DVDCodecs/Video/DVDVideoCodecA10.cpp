#include "DVDVideoCodecA10.h"
#include "DVDClock.h"
#include "utils/log.h"
#include "threads/Atomics.h"
#include "../VideoRenderers/LinuxRendererA10.h"
//check for eden
#include "../VideoRenderers/RenderFlags.h"
#ifdef CONF_FLAGS_FORMAT_A10BUF
#define RENDER_FMT_YUV420P      DVDVideoPicture::FMT_YUV420P
#define RENDER_FMT_A10BUF       DVDVideoPicture::FMT_A10BUF
#endif

#include <sys/ioctl.h>
#include <math.h>

static long g_cedaropen = 0;

#define PREFRAMES 10 //no. of stream frames to buffer

#define A10DEBUG
#define MEDIAINFO

#define CODEC_OK        1
#define CODEC_BAD       0

/*Cedar Decoder*/
#define A10ENABLE_MPEG1         CODEC_OK
#define A10ENABLE_MPEG2         CODEC_OK
#define A10ENABLE_H264          CODEC_OK
#define A10ENABLE_H263          CODEC_BAD //fails completely
#define A10ENABLE_VC1_WVC1      CODEC_OK
#define A10ENABLE_VP6           CODEC_OK
#define A10ENABLE_VP8           CODEC_OK
#define A10ENABLE_FLV1          CODEC_OK
#define A10ENABLE_MJPEG         CODEC_OK
#define A10ENABLE_WMV1          CODEC_OK
#define A10ENABLE_WMV2          CODEC_OK
#define A10ENABLE_WMV3          CODEC_OK
#define A10ENABLE_MPEG4V1       CODEC_BAD //??
#define A10ENABLE_MPEG4V2       CODEC_BAD //fails completely
#define A10ENABLE_MPEG4V3       CODEC_OK
#define A10ENABLE_DIVX4         CODEC_OK
#define A10ENABLE_DIVX5         CODEC_OK
#define A10ENABLE_XVID          CODEC_OK

/*
TODO:- Finish adding MPEG4 codecs tags 
     - Find out whats causing problems with several codecs, something is wrong or missing in the maping.
     - Add RealVideo once .rm files can be opened.
     - AVS and RMG2 codec support.

Note: AllWinner doc says to add FLV container type to VP6 and FLV1, but if i do so they stop working.
*/

#define _4CC(c1,c2,c3,c4) (((u32)(c4)<<24)|((u32)(c3)<<16)|((u32)(c2)<<8)|(u32)(c1))

static void freecallback(void *callbackpriv, void *pictpriv, cedarv_picture_t &pict)
{
  ((CDVDVideoCodecA10*)callbackpriv)->FreePicture(pictpriv, pict);
}

CDVDVideoCodecA10::CDVDVideoCodecA10()
{
  m_hcedarv  = NULL;
  memset(&m_picture, 0, sizeof(m_picture));
}

CDVDVideoCodecA10::~CDVDVideoCodecA10()
{
  Dispose();
}

/*
 * Open the decoder, returns true on success
 */
bool CDVDVideoCodecA10::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{
  if (getenv("NOA10"))
  {
    CLog::Log(LOGNOTICE, "A10: disabled.\n");
    return false;
  }

  if (hints.software)
  {
    CLog::Log(LOGNOTICE, "A10: software decoding requested.\n");
    return false;
  }

  CLog::Log(LOGNOTICE, "A10: using hardware rendering.\n");

  m_hints  = hints;
  m_aspect = m_hints.aspect;

  memset(&m_info, 0, sizeof(m_info));
  if (m_hints.fpsscale)
  {
    m_info.frame_rate = (double)m_hints.fpsrate / m_hints.fpsscale * 1000;
  }
  if (m_info.frame_rate)
  {
    m_info.frame_duration = 1000000000.0 / m_info.frame_rate;
  }
  m_info.video_width = m_hints.width;
  m_info.video_height = m_hints.height;
  m_info.aspect_ratio = 1000;
  m_info.is_pts_correct = !m_hints.ptsinvalid;
  m_info.sub_format = CEDARV_SUB_FORMAT_UNKNOW;
  m_info.container_format = CEDARV_CONTAINER_FORMAT_UNKNOW;
  m_info.init_data_len = 0;
  m_info.init_data = NULL;

#ifdef MEDIAINFO
  CLog::Log(LOGDEBUG, "A10: MEDIAINFO: fpsrate %d / fpsscale %d\n", m_hints.fpsrate, m_hints.fpsscale);
  CLog::Log(LOGDEBUG, "A10: MEDIAINFO: CodecID %d \n", m_hints.codec);
  CLog::Log(LOGDEBUG, "A10: MEDIAINFO: StreamType %d \n", m_hints.type);
  CLog::Log(LOGDEBUG, "A10: MEDIAINFO: Level %d \n", m_hints.level);
  CLog::Log(LOGDEBUG, "A10: MEDIAINFO: Profile %d \n", m_hints.profile);
  CLog::Log(LOGDEBUG, "A10: MEDIAINFO: PTS_invalid %d \n", m_hints.ptsinvalid);
  CLog::Log(LOGDEBUG, "A10: MEDIAINFO: Tag %d \n", m_hints.codec_tag);
  { u8 *pb = (u8*)&m_hints.codec_tag;
    if (isalnum(pb[0]) && isalnum(pb[1]) && isalnum(pb[2]) && isalnum(pb[3]))
      CLog::Log(LOGDEBUG, "A10: MEDIAINFO: Tag fourcc %c%c%c%c\n", pb[0], pb[1], pb[2], pb[3]);
  }
  if (m_hints.extrasize)
  {
    int  i;
    char buf[4096];

    for (i = 0; i < m_hints.extrasize; i++)
      sprintf(buf+i*2, "%02x", ((u8*)m_hints.extradata)[i]);
    CLog::Log(LOGDEBUG, "A10: MEDIAINFO: extradata %d %s\n", m_hints.extrasize, buf);
  }
#endif

  m_info.init_data_len = m_hints.extrasize;
  m_info.init_data     = (u8*)m_hints.extradata;

  switch(m_hints.codec)
  {
  //MPEG1
#if A10ENABLE_MPEG1
  case CODEC_ID_MPEG1VIDEO:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG2;
    m_info.sub_format = CEDARV_MPEG2_SUB_FORMAT_MPEG1;
    break;
#endif
    //MPEG2
#ifdef A10ENABLE_MPEG2
  case CODEC_ID_MPEG2VIDEO:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG2;
    m_info.sub_format = CEDARV_MPEG2_SUB_FORMAT_MPEG2;
    //if passing init_data it fails..
    m_info.init_data_len = 0;
    m_info.init_data = NULL;
    break;
#endif
    //H263
#if A10ENABLE_H263
  case CODEC_ID_H263:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_H263;
    break;
#endif
    //H264
#if A10ENABLE_H264
  case CODEC_ID_H264:
    m_info.format = CEDARV_STREAM_FORMAT_H264;
    if(m_hints.codec_tag==27) //M2TS and TS
      m_info.container_format = CEDARV_CONTAINER_FORMAT_TS;
    break;
#endif
    //VP6
#if A10ENABLE_VP6
  case CODEC_ID_VP6F:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_VP6;
    break;
#endif
    //WMV1
#if A10ENABLE_WMV1
  case CODEC_ID_WMV1:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_WMV1;
    break;
#endif
    //WMV2
#if A10ENABLE_WMV2
  case CODEC_ID_WMV2:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_WMV2;
    break;
#endif
    //WMV3
#if A10ENABLE_WMV3
  case CODEC_ID_WMV3:
    m_info.format = CEDARV_STREAM_FORMAT_VC1;
    break;
#endif
    //VC1 and WVC1
#if A10ENABLE_VC1_WVC1
  case CODEC_ID_VC1:
    m_info.format = CEDARV_STREAM_FORMAT_VC1;
    break;
#endif
    //MJPEG
#if A10ENABLE_MJPEG
  case CODEC_ID_MJPEG:
    m_info.format = CEDARV_STREAM_FORMAT_MJPEG;
    break;
#endif
    //VP8
#if A10ENABLE_VP8
  case CODEC_ID_VP8:
    m_info.format = CEDARV_STREAM_FORMAT_VP8;
    break;
#endif
    //MSMPEG4V1
#if A10ENABLE_MPEG4V1
  case CODEC_ID_MSMPEG4V1:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_DIVX1;
    break;
#endif
    //MSMPEG4V2
#if A10ENABLE_MPEG4V2
  case CODEC_ID_MSMPEG4V2:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_DIVX2;
    break;
#endif
    //MSMPEG4V3
#if A10ENABLE_MPEG4V3
  case CODEC_ID_MSMPEG4V3:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_DIVX3;
    break;
#endif
    //Sorensson Spark (FLV1)
#if A10ENABLE_FLV1
  case CODEC_ID_FLV1:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_SORENSSON_H263;
    break;
#endif
    //Detected as MPEG4 (ID 13)
  case CODEC_ID_MPEG4:
    m_info.format = CEDARV_STREAM_FORMAT_MPEG4;
    switch(m_hints.codec_tag)
    {
    //DX40/DIVX4, divx
#if A10ENABLE_DIVX4
    case _4CC('D','I','V','X'):
      m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_DIVX4;
      break;
#endif
    //DX50/DIVX5
#if A10ENABLE_DIVX5
    case _4CC('D','X','5','0'):
    case _4CC('D','I','V','5'):
      m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_DIVX5;
      break;
#endif
   //XVID
#if A10ENABLE_XVID
    case _4CC('X','V','I','D'):
    case _4CC('M','P','4','V'):
    case _4CC('P','M','P','4'):
    case _4CC('F','M','P','4'):
      m_info.sub_format = CEDARV_MPEG4_SUB_FORMAT_XVID;
      break;
#endif
    default:
      CLog::Log(LOGERROR, "A10: MPEG4 codec tag %d is unknown.\n", m_hints.codec_tag);
      return false;
    }
    break;

  default:
    CLog::Log(LOGERROR, "A10: codecid %d is unknown.\n", m_hints.codec);
    return false;
  }

  return DoOpen();
}

bool CDVDVideoCodecA10::DoOpen()
{
  s32 ret;

  if (cas(&g_cedaropen, 0, 1) != 0)
  {
    CLog::Log(LOGERROR, "A10: cedar already in use");
    return false;
  }

  m_hcedarv = libcedarv_init(&ret);
  if (ret < 0)
  {
    CLog::Log(LOGERROR, "A10: libcedarv_init failed. (%d)\n", ret);
    goto Error;
  }

  ret = m_hcedarv->set_vstream_info(m_hcedarv, &m_info);
  if (ret < 0)
  {
    CLog::Log(LOGERROR, "A10: set_vstream_info failed. (%d)\n", ret);
    goto Error;
  }

  ret = m_hcedarv->open(m_hcedarv);
  if (ret < 0)
  {
    CLog::Log(LOGERROR, "A10: open failed. (%d)\n", ret);
    goto Error;
  }

  m_hcedarv->ioctrl(m_hcedarv, CEDARV_COMMAND_RESET, 0);

  ret = m_hcedarv->ioctrl(m_hcedarv, CEDARV_COMMAND_PLAY, 0);
  if (ret < 0)
  {
    CLog::Log(LOGERROR, "A10: CEDARV_COMMAND_PLAY failed. (%d)\n", ret);
    goto Error;
  }

  m_prebuffer = true;
  m_nframes   = 0;

  CLog::Log(LOGDEBUG, "A10: cedar open.");
  return true;

Error:

  Dispose();
  return false;
}

/*
 * Dispose, Free all resources
 */
void CDVDVideoCodecA10::Dispose()
{
  A10VLFreeQueue();
  if (m_hcedarv)
  {
    m_hcedarv->ioctrl(m_hcedarv, CEDARV_COMMAND_STOP, 0);
    m_hcedarv->close(m_hcedarv);
    libcedarv_exit(m_hcedarv);
    m_hcedarv = NULL;
    cas(&g_cedaropen, 1, 0);
    CLog::Log(LOGDEBUG, "A10: cedar dispose.");
  }
}

/*
 * returns one or a combination of VC_ messages
 * pData and iSize can be NULL, this means we should flush the rest of the data.
 */
int CDVDVideoCodecA10::Decode(BYTE* pData, int iSize, double dts, double pts)
{
  int                        status = 0;
  s32                        ret = 0;
  u8                        *buf0, *buf1;
  u32                        bufsize0, bufsize1;
  cedarv_stream_data_info_t  dinf;
  cedarv_picture_t           picture;

  if (!m_hcedarv)
    return VC_ERROR;

  if (m_nframes == 0)
  {
    m_hcedarv->decode(m_hcedarv);
    m_hcedarv->decode(m_hcedarv);
  }

  if (pData && iSize)
  {
    ret = m_hcedarv->request_write(m_hcedarv, iSize, &buf0, &bufsize0, &buf1, &bufsize1);
    if(ret < 0)
    {
      CLog::Log(LOGERROR, "A10: request_write failed.\n");
      return VC_ERROR;
    }
    if (bufsize1)
    {
      memcpy(buf0, pData, bufsize0);
      memcpy(buf1, pData+bufsize0, bufsize1);
    }
    else
    {
      memcpy(buf0, pData, iSize);
    }

    memset(&dinf, 0, sizeof(dinf));
    dinf.lengh = iSize;
#ifdef CEDARV_FLAG_DECODE_NO_DELAY
    dinf.flags = CEDARV_FLAG_FIRST_PART | CEDARV_FLAG_LAST_PART | CEDARV_FLAG_DECODE_NO_DELAY;
#else
    dinf.flags = CEDARV_FLAG_FIRST_PART | CEDARV_FLAG_LAST_PART;
#endif
    if (pts != DVD_NOPTS_VALUE)
    {
      dinf.flags |= CEDARV_FLAG_PTS_VALID;
      dinf.pts = pts;
    }
    else if (dts != DVD_NOPTS_VALUE)
    {
      dinf.flags |= CEDARV_FLAG_PTS_VALID;
      dinf.pts = dts;
    }

    ret = m_hcedarv->update_data(m_hcedarv, &dinf);
    if (ret)
    {
      CLog::Log(LOGERROR, "A10: update_data(): %d\n", ret);
    }

    if (m_prebuffer)
    {
      m_nframes++;
      status |= VC_BUFFER;
      if (m_nframes >= PREFRAMES)
        m_prebuffer = false;
    }
  }

  if (!m_prebuffer)
  {
    ret = m_hcedarv->decode(m_hcedarv);
    switch(ret)
    {
    case CEDARV_RESULT_OK:
    case CEDARV_RESULT_FRAME_DECODED:
    case CEDARV_RESULT_KEYFRAME_DECODED:
    case CEDARV_RESULT_NO_BITSTREAM:
      status |= VC_BUFFER;
      break;
    case CEDARV_RESULT_NO_FRAME_BUFFER:
      CLog::Log(LOGNOTICE, "A10: no frames. free queue.");
      A10VLFreeQueue();
      //ret = m_hcedarv->decode(m_hcedarv);
      break;

    default:
      CLog::Log(LOGERROR, "A10: decode(%d): %d\n", iSize, ret);
      break;
    }

    ret = m_hcedarv->display_request(m_hcedarv, &picture);
    if (ret > 3 || ret < -1)
    {
      CLog::Log(LOGERROR, "A10: display_request(): %d\n", ret);
    }
    else if (ret == 0)
    {
      float aspect_ratio = m_aspect;

      m_picture.dts     = DVD_NOPTS_VALUE;
      m_picture.pts     = picture.pts > 0 ? (double)picture.pts : DVD_NOPTS_VALUE;
      m_picture.iWidth  = picture.display_width;
      m_picture.iHeight = picture.display_height;

      if (picture.is_progressive) m_picture.iFlags &= ~DVP_FLAG_INTERLACED;
      else                        m_picture.iFlags |= DVP_FLAG_INTERLACED;

      /* XXX: we suppose the screen has a 1.0 pixel ratio */ // CDVDVideo will compensate it.
      if (aspect_ratio <= 0.0)
        aspect_ratio = (float)m_picture.iWidth / (float)m_picture.iHeight;

      m_picture.iDisplayHeight = m_picture.iHeight;
      m_picture.iDisplayWidth  = ((int)lrint(m_picture.iHeight * aspect_ratio)) & -3;
      if (m_picture.iDisplayWidth > m_picture.iWidth)
      {
        m_picture.iDisplayWidth  = m_picture.iWidth;
        m_picture.iDisplayHeight = ((int)lrint(m_picture.iWidth / aspect_ratio)) & -3;
      }

      m_picture.format     = RENDER_FMT_A10BUF;
      m_picture.a10buffer  = A10VLPutQueue(freecallback, (void*)this, NULL, picture);
      m_picture.iFlags    |= DVP_FLAG_ALLOCATED;

      //CLog::Log(LOGDEBUG, "A10: decode %d\n", buffer->picture.id);

      status |= VC_PICTURE;
    }
  }

  return status;
}

/*
 * Reset the decoder.
 * Should be the same as calling Dispose and Open after each other
 */
void CDVDVideoCodecA10::Reset()
{
  cedarv_picture_t pict;

  CLog::Log(LOGDEBUG, "A10: reset requested");
  m_hcedarv->ioctrl(m_hcedarv, CEDARV_COMMAND_FLUSH, 0);
  while(m_hcedarv->display_request(m_hcedarv, &pict) == 0)
    m_hcedarv->display_release(m_hcedarv, pict.id);
  A10VLFreeQueue();
}

/*
 * returns true if successfull
 * the data is valid until the next Decode call
 */
bool CDVDVideoCodecA10::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  if (m_picture.iFlags & DVP_FLAG_ALLOCATED)
  {
    *pDvdVideoPicture = m_picture;
    return true;
  }
  return false;
}

bool CDVDVideoCodecA10::ClearPicture(DVDVideoPicture* pDvdVideoPicture)
{
  return CDVDVideoCodec::ClearPicture(pDvdVideoPicture);
}

void CDVDVideoCodecA10::SetDropState(bool bDrop)
{
}

const char* CDVDVideoCodecA10::GetName()
{
  return "A10";
}

void CDVDVideoCodecA10::FreePicture(void *pictpriv, cedarv_picture_t &pict)
{
  m_hcedarv->display_release(m_hcedarv, pict.id);
}

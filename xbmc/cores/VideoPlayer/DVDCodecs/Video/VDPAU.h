/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

/**
 * design goals:
 * - improve performance
 *   max out hw resources: e.g. make 1080p60 play on ION2
 *   allow advanced de-interlacing on ION
 *
 * - add vdpau/opengl interop
 *
 * - remove tight dependency to render thread
 *   prior design needed to hijack render thread in order to do
 *   gl interop functions. In particular this was a problem for
 *   init and clear down. Introduction of GL_NV_vdpau_interop
 *   increased the need to be independent from render thread
 *
 * - move to an actor based design in order to reduce the number
 *   of locks needed.
 */

#pragma once

#include "system_gl.h"
#define GLX_GLXEXT_PROTOTYPES
#include "DVDVideoCodec.h"
#include "DVDVideoCodecFFmpeg.h"
#include "libavcodec/vdpau.h"
#if HAS_GL
#include <GL/glx.h>
#include <GL/glext.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif
#if HAS_GLES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#endif
#include "DVDVideoCodec.h"
#include "DVDVideoCodecFFmpeg.h"
#include "threads/CriticalSection.h"
#include "threads/SharedSection.h"
#include "settings/VideoSettings.h"
#include "guilib/DispResource.h"
#include "threads/Event.h"
#include "threads/Thread.h"
#include "utils/ActorProtocol.h"
#include "guilib/Geometry.h"
#include <list>
#include <map>

extern "C" {
#include "libavutil/avutil.h"
#include "libavcodec/vdpau.h"
}

#define FULLHD_WIDTH                       1920
#define MAX_PIC_Q_LENGTH                   20 //for non-interop_yuv this controls the max length of the decoded pic to render completion Q

#define GL_NV_vdpau_sim_interop 1
#define GL_OES_EGL_sync         1

#if defined(GL_NV_vdpau_sim_interop)
#ifndef GL_APIENTRYP
#   define GL_APIENTRYP GL_APIENTRY*
#endif

typedef GLintptr  GLvdpauSurfaceNV;
#define GL_READ_ONLY    0x88B8
#define GL_WRITE_ONLY   0x88B9
#define GL_READ_WRITE   0x88BA

GL_APICALL void GL_APIENTRY glEGLImageTargetTexture2DOES (GLenum target, GLeglImageOES image);

typedef void (GL_APIENTRYP PFNGLVDPAUFININVPROC) (void);
typedef void (GL_APIENTRYP PFNGLVDPAUGETSURFACEIVNVPROC) (GLvdpauSurfaceNV surface, GLenum pname, GLsizei bufSize, GLsizei* length, GLint *values);
#if GL_NV_vdpau_sim_interop
  typedef void (GL_APIENTRYP PFNGLVDPAUINITNVPROC) (const void* vdpDevice, const GLvoid*getProcAddress, EGLContext shared_context);
#else
  typedef void (GL_APIENTRYP PFNGLVDPAUINITNVPROC) (const void* vdpDevice, const GLvoid*getProcAddress);
#endif
typedef void (GL_APIENTRYP PFNGLVDPAUISSURFACENVPROC) (GLvdpauSurfaceNV surface);
typedef void (GL_APIENTRYP PFNGLVDPAUMAPSURFACESNVPROC) (GLsizei numSurfaces, const GLvdpauSurfaceNV* surfaces);
typedef GLvdpauSurfaceNV (GL_APIENTRYP PFNGLVDPAUREGISTEROUTPUTSURFACENVPROC) (const void* vdpSurface, GLenum target, GLsizei numTextureNames, const GLuint *textureNames);
typedef GLvdpauSurfaceNV (GL_APIENTRYP PFNGLVDPAUREGISTERVIDEOSURFACENVPROC) (const void* vdpSurface, GLenum target, GLsizei numTextureNames, const GLuint *textureNames);
typedef void (GL_APIENTRYP PFNGLVDPAUSURFACEACCESSNVPROC) (GLvdpauSurfaceNV surface, GLenum access);
typedef void (GL_APIENTRYP PFNGLVDPAUUNMAPSURFACESNVPROC) (GLsizei numSurface, const GLvdpauSurfaceNV* surfaces);
typedef void (GL_APIENTRYP PFNGLVDPAUUNREGISTERSURFACENVPROC) (GLvdpauSurfaceNV surface);

typedef VdpStatus (*PVDPDEVICEOPENGLESNVOPEN)(EGLDisplay _eglDisplay, VdpGetProcAddress **get_proc_address);
#endif

namespace VDPAU
{

/**
 * VDPAU interface to driver
 */

struct VDPAU_procs
{
  VdpGetProcAddress *                   vdp_get_proc_address;
  VdpDeviceDestroy *                    vdp_device_destroy;

  VdpVideoSurfaceCreate *               vdp_video_surface_create;
  VdpVideoSurfaceDestroy *              vdp_video_surface_destroy;
  VdpVideoSurfacePutBitsYCbCr *         vdp_video_surface_put_bits_y_cb_cr;
  VdpVideoSurfaceGetBitsYCbCr *         vdp_video_surface_get_bits_y_cb_cr;

  VdpOutputSurfacePutBitsYCbCr *        vdp_output_surface_put_bits_y_cb_cr;
  VdpOutputSurfacePutBitsNative *       vdp_output_surface_put_bits_native;
  VdpOutputSurfaceCreate *              vdp_output_surface_create;
  VdpOutputSurfaceDestroy *             vdp_output_surface_destroy;
  VdpOutputSurfaceGetBitsNative *       vdp_output_surface_get_bits_native;
  VdpOutputSurfaceRenderOutputSurface * vdp_output_surface_render_output_surface;
  VdpOutputSurfacePutBitsIndexed *      vdp_output_surface_put_bits_indexed;

  VdpVideoMixerCreate *                 vdp_video_mixer_create;
  VdpVideoMixerSetFeatureEnables *      vdp_video_mixer_set_feature_enables;
  VdpVideoMixerQueryParameterSupport *  vdp_video_mixer_query_parameter_support;
  VdpVideoMixerQueryFeatureSupport *    vdp_video_mixer_query_feature_support;
  VdpVideoMixerDestroy *                vdp_video_mixer_destroy;
  VdpVideoMixerRender *                 vdp_video_mixer_render;
  VdpVideoMixerSetAttributeValues *     vdp_video_mixer_set_attribute_values;

  VdpGenerateCSCMatrix *                vdp_generate_csc_matrix;

  VdpGetErrorString *                         vdp_get_error_string;

  VdpDecoderCreate *             vdp_decoder_create;
  VdpDecoderDestroy *            vdp_decoder_destroy;
  VdpDecoderRender *             vdp_decoder_render;
  VdpDecoderQueryCapabilities *  vdp_decoder_query_caps;

  VdpPreemptionCallbackRegister * vdp_preemption_callback_register;
  
  VdpPresentationQueueTargetDestroy *           vdp_presentation_queue_target_destroy;
  VdpPresentationQueueCreate *                  vdp_presentation_queue_create;
  VdpPresentationQueueDestroy *                 vdp_presentation_queue_destroy;
  VdpPresentationQueueDisplay *                 vdp_presentation_queue_display;
  VdpPresentationQueueBlockUntilSurfaceIdle *   vdp_presentation_queue_block_until_surface_idle;
  VdpPresentationQueueTargetCreateX11 *         vdp_presentation_queue_target_create_x11;
  VdpPresentationQueueQuerySurfaceStatus *      vdp_presentation_queue_query_surface_status;
  VdpPresentationQueueGetTime *                 vdp_presentation_queue_get_time;
  VdpDecoderSetControlData *                    vdp_decoder_set_video_control_data;

};

//-----------------------------------------------------------------------------
// VDPAU data structs
//-----------------------------------------------------------------------------

class CDecoder;

/**
 * Buffer statistics used to control number of frames in queue
 */

class CVdpauBufferStats
{
public:
  uint16_t decodedPics;
  uint16_t processedPics;
  uint16_t renderPics;
  uint64_t latency;         // time decoder has waited for a frame, ideally there is no latency
  int codecFlags;
  bool canSkipDeint;
  bool draining;

  void IncDecoded() { CSingleLock l(m_sec); decodedPics++;}
  void DecDecoded() { CSingleLock l(m_sec); decodedPics--;}
  void IncProcessed() { CSingleLock l(m_sec); processedPics++;}
  void DecProcessed() { CSingleLock l(m_sec); processedPics--;}
  void IncRender() { CSingleLock l(m_sec); renderPics++;}
  void DecRender() { CSingleLock l(m_sec); renderPics--;}
  void Reset() { CSingleLock l(m_sec); decodedPics=0; processedPics=0;renderPics=0;latency=0;}
  void Get(uint16_t &decoded, uint16_t &processed, uint16_t &render) {CSingleLock l(m_sec); decoded = decodedPics, processed=processedPics, render=renderPics;}
  void SetParams(uint64_t time, int flags) { CSingleLock l(m_sec); latency = time; codecFlags = flags; }
  void GetParams(uint64_t &lat, int &flags) { CSingleLock l(m_sec); lat = latency; flags = codecFlags; }
  void SetCanSkipDeint(bool canSkip) { CSingleLock l(m_sec); canSkipDeint = canSkip; }
  bool CanSkipDeint() { CSingleLock l(m_sec); if (canSkipDeint) return true; else return false;}
  void SetDraining(bool drain) { CSingleLock l(m_sec); draining = drain; }
  bool IsDraining() { CSingleLock l(m_sec); if (draining) return true; else return false;}
private:
  CCriticalSection m_sec;
};

/**
 *  CVdpauConfig holds all configuration parameters needed by vdpau
 *  The structure is sent to the internal classes CMixer and COutput
 *  for init.
 */

class CVideoSurfaces;
class CVDPAUContext;

struct CVdpauConfig
{
  int surfaceWidth;
  int surfaceHeight;
  int vidWidth;
  int vidHeight;
  int outWidth;
  int outHeight;
  VdpDecoder vdpDecoder;
  VdpChromaType vdpChromaType;
  CVdpauBufferStats *stats;
  CDecoder *vdpau;
  int upscale;
  CVideoSurfaces *videoSurfaces;
  int numRenderBuffers;
  uint32_t maxReferences;
  bool useInteropYuv;
  CVDPAUContext *context;
};

/**
 * Holds a decoded frame
 * Input to COutput for further processing
 */
struct CVdpauDecodedPicture
{
  DVDVideoPicture DVDPic;
  VdpVideoSurface videoSurface;
};

/**
 * Frame after having been processed by vdpau mixer
 */
struct CVdpauProcessedPicture
{
  DVDVideoPicture DVDPic;
  VdpVideoSurface videoSurface;
  VdpOutputSurface outputSurface;
  bool crop;
};

/**
 * Ready to render textures
 * Sent from COutput back to CDecoder
 * Objects are referenced by DVDVideoPicture and are sent
 * to renderer
 */
class CVdpauRenderPicture
{
  friend class CDecoder;
  friend class COutput;
public:
  CVdpauRenderPicture(CCriticalSection &section)
    : refCount(0), renderPicSection(section) { 
#if HAS_GL
      fence = None;
#endif
    }
  void Sync();
  DVDVideoPicture DVDPic;
  int texWidth, texHeight;
  CRect crop;
  static const int MAX_NUM_TEXTURES = 6;
  int numTextures;
  GLuint texture[MAX_NUM_TEXTURES];
  uint32_t sourceIdx;
  bool valid;
  CDecoder *vdpau;
  CVdpauRenderPicture* Acquire();
  long Release();
private:
  void ReturnUnused();
  bool usefence;
#if HAS_GL
  GLsync fence;
#endif
#if GL_OES_EGL_sync
  EGLSyncKHR fence;
#endif
  int refCount;
  CCriticalSection &renderPicSection;
};

//-----------------------------------------------------------------------------
// Mixer
//-----------------------------------------------------------------------------

class CMixerControlProtocol : public Actor::Protocol
{
public:
  CMixerControlProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    INIT = 0,
    FLUSH,
    TIMEOUT,
  };
  enum InSignal
  {
    ACC,
    ERROR,
  };
};

class CMixerDataProtocol : public Actor::Protocol
{
public:
  CMixerDataProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    FRAME,
    BUFFER,
  };
  enum InSignal
  {
    PICTURE,
  };
};

/**
 * Embeds the vdpau video mixer
 * Embedded by COutput class, gets decoded frames from COutput, processes
 * them in mixer ands sends processed frames back to COutput
 */
class CMixer : private CThread
{
public:
  CMixer(CEvent *inMsgEvent);
  virtual ~CMixer();
  void Start();
  void Dispose();
  bool IsActive();
  CMixerControlProtocol m_controlPort;
  CMixerDataProtocol m_dataPort;
protected:
  void OnStartup();
  void OnExit();
  void Process();
  void StateMachine(int signal, Actor::Protocol *port, Actor::Message *msg);
  void Init();
  void Uninit();
  void Flush();
  void CreateVdpauMixer();
  void ProcessPicture();
  void InitCycle();
  void FiniCycle();
  void CheckFeatures();
  void SetPostProcFeatures(bool postProcEnabled);
  void PostProcOff();
  void InitCSCMatrix(int Width);
  bool GenerateStudioCSCMatrix(VdpColorStandard colorStandard, VdpCSCMatrix &studioCSCMatrix);
  void SetColor();
  void SetNoiseReduction();
  void SetSharpness();
  void SetDeintSkipChroma();
  void SetDeinterlacing();
  void SetHWUpscaling();
  void DisableHQScaling();
  EINTERLACEMETHOD GetDeinterlacingMethod(bool log = false);
  bool CheckStatus(VdpStatus vdp_st, int line);
  CEvent m_outMsgEvent;
  CEvent *m_inMsgEvent;
  int m_state;
  bool m_bStateMachineSelfTrigger;

  // extended state variables for state machine
  int m_extTimeout;
  bool m_vdpError;
  CVdpauConfig m_config;
  VdpVideoMixer m_videoMixer;
  VdpProcamp m_Procamp;
  VdpCSCMatrix  m_CSCMatrix;
  bool m_PostProc;
  float m_Brightness;
  float m_Contrast;
  float m_NoiseReduction;
  float m_Sharpness;
  int m_DeintMode;
  int m_Deint;
  int m_Upscale;
  bool m_SeenInterlaceFlag;
  unsigned int m_ColorMatrix       : 4;
  VdpVideoMixerPictureStructure m_mixerfield;
  int m_mixerstep;
  int m_mixersteps;
  CVdpauProcessedPicture m_processPicture;
  std::queue<VdpOutputSurface> m_outputSurfaces;
  std::queue<CVdpauDecodedPicture> m_decodedPics;
  std::deque<CVdpauDecodedPicture> m_mixerInput;
};

//-----------------------------------------------------------------------------
// Output
//-----------------------------------------------------------------------------

/**
 * Buffer pool holds allocated vdpau and gl resources
 * Embedded in COutput
 */
struct VdpauBufferPool
{
  VdpauBufferPool();
  virtual ~VdpauBufferPool();
  static const int MAX_NUM_TEXTURES = 6;
  struct GLVideoSurface
  {
    int numTextures;
    GLuint texture[MAX_NUM_TEXTURES];
#if defined(GL_NV_vdpau_interop) || defined(GL_NV_vdpau_sim_interop)
    GLvdpauSurfaceNV glVdpauSurface;
#endif
    VdpVideoSurface sourceVuv;
    VdpOutputSurface sourceRgb;
  };
  std::vector<CVdpauRenderPicture*> allRenderPics;
  unsigned short numOutputSurfaces;
  std::vector<VdpOutputSurface> outputSurfaces;
  std::map<VdpVideoSurface, GLVideoSurface> glVideoSurfaceMap;
  std::map<VdpOutputSurface, GLVideoSurface> glOutputSurfaceMap;
  std::queue<CVdpauProcessedPicture> processedPics;
  std::deque<int> usedRenderPics;
  std::deque<int> freeRenderPics;
  std::deque<int> syncRenderPics;
  CCriticalSection renderPicSec;
};

class COutputControlProtocol : public Actor::Protocol
{
public:
  COutputControlProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Actor::Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    INIT,
    FLUSH,
    PRECLEANUP,
    TIMEOUT,
  };
  enum InSignal
  {
    ACC,
    ERROR,
    STATS,
  };
};

class COutputDataProtocol : public Actor::Protocol
{
public:
  COutputDataProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Actor::Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    NEWFRAME = 0,
    RETURNPIC,
  };
  enum InSignal
  {
    PICTURE,
  };
};

/**
 * COutput is embedded in CDecoder and embeds CMixer
 * The class has its own OpenGl context which is shared with render thread
 * COuput generated ready to render textures and passes them back to
 * CDecoder
 */
class COutput : private CThread
{
public:
  COutput(CEvent *inMsgEvent);
  virtual ~COutput();
  void Start();
  void Dispose();
  COutputControlProtocol m_controlPort;
  COutputDataProtocol m_dataPort;
protected:
  void OnStartup();
  void OnExit();
  void Process();
  void StateMachine(int signal, Actor::Protocol *port, Actor::Message *msg);
  bool HasWork();
  CVdpauRenderPicture *ProcessMixerPicture();
  void QueueReturnPicture(CVdpauRenderPicture *pic);
  void ProcessReturnPicture(CVdpauRenderPicture *pic);
  bool ProcessSyncPicture();
  bool Init();
  bool Uninit();
  void Flush();
  bool CreateGlxContext();
  bool DestroyGlxContext();
  bool EnsureBufferPool();
  void ReleaseBufferPool();
  void PreCleanup();
  void InitMixer();
  bool GLInit();
  void GLMapSurface(bool yuv, uint32_t source);
  void GLUnmapSurfaces();
  bool CheckStatus(VdpStatus vdp_st, int line);
  CEvent m_outMsgEvent;
  CEvent *m_inMsgEvent;
  int m_state;
  bool m_bStateMachineSelfTrigger;

  // extended state variables for state machine
  int m_extTimeout;
  bool m_vdpError;
  CVdpauConfig m_config;
  VdpauBufferPool m_bufferPool;
  CMixer m_mixer;
#if HAS_GL
  Display *m_Display;
  Window m_Window;
  GLXContext m_glContext;
  GLXWindow m_glWindow;
  Pixmap    m_pixmap;
  GLXPixmap m_glPixmap;
#endif
#if HAS_GLES
  EGLDisplay m_Display;
  EGLContext m_context;
#endif
  // gl functions
#if defined(GL_NV_vdpau_interop) || defined(GL_NV_vdpau_sim_interop)
  PFNGLVDPAUINITNVPROC glVDPAUInitNV;
  PFNGLVDPAUFININVPROC glVDPAUFiniNV;
  PFNGLVDPAUREGISTEROUTPUTSURFACENVPROC glVDPAURegisterOutputSurfaceNV;
  PFNGLVDPAUREGISTERVIDEOSURFACENVPROC glVDPAURegisterVideoSurfaceNV;
  PFNGLVDPAUISSURFACENVPROC glVDPAUIsSurfaceNV;
  PFNGLVDPAUUNREGISTERSURFACENVPROC glVDPAUUnregisterSurfaceNV;
  PFNGLVDPAUSURFACEACCESSNVPROC glVDPAUSurfaceAccessNV;
  PFNGLVDPAUMAPSURFACESNVPROC glVDPAUMapSurfacesNV;
  PFNGLVDPAUUNMAPSURFACESNVPROC glVDPAUUnmapSurfacesNV;
  PFNGLVDPAUGETSURFACEIVNVPROC glVDPAUGetSurfaceivNV;
#endif
#if defined(GL_NV_vdpau_sim_interop)
  PVDPDEVICEOPENGLESNVOPEN nv_ndpau_device_open;
#endif


#ifdef GL_NV_vdpau_sim_interop
  void *m_dlVdpauNvHandle;
  void* GLNVGetProcAddress(const char * func);
#endif
};

//-----------------------------------------------------------------------------
// VDPAU Video Surface states
//-----------------------------------------------------------------------------

class CVideoSurfaces
{
public:
  void AddSurface(VdpVideoSurface surf);
  void ClearReference(VdpVideoSurface surf);
  bool MarkRender(VdpVideoSurface surf);
  void ClearRender(VdpVideoSurface surf);
  bool IsValid(VdpVideoSurface surf);
  VdpVideoSurface GetFree(VdpVideoSurface surf);
  VdpVideoSurface RemoveNext(bool skiprender = false);
  void Reset();
  int Size();
protected:
  std::map<VdpVideoSurface, int> m_state;
  std::list<VdpVideoSurface> m_freeSurfaces;
  CCriticalSection m_section;
};

//-----------------------------------------------------------------------------
// VDPAU decoder
//-----------------------------------------------------------------------------

class CVDPAUContext
{
public:
  static bool EnsureContext(CVDPAUContext **ctx);
  void Release();
  VDPAU_procs& GetProcs();
  VdpDevice GetDevice();
  bool Supports(VdpVideoMixerFeature feature);
  VdpVideoMixerFeature* GetFeatures();
  int GetFeatureCount();
  VdpPresentationQueueTarget   m_vdpFlipTarget;
  VdpPresentationQueue         m_vdpFlipQueue;
private:
  CVDPAUContext();
  void Close();
  bool LoadSymbols();
  bool CreateContext();
  void DestroyContext();
  void QueryProcs();
  void SpewHardwareAvailable();
  bool CheckStatus(VdpStatus vdp_st, int line);
  static CVDPAUContext *m_context;
  static CCriticalSection m_section;
  static Display *m_display;
  int m_refCount;
  VdpVideoMixerFeature m_vdpFeatures[14];
  int m_featureCount;
  static void *m_dlHandle;
  VdpDevice m_vdpDevice;
  VDPAU_procs m_vdpProcs;
  VdpStatus (*dl_vdp_device_create_x11)(Display* display, int screen, VdpDevice* device, VdpGetProcAddress **get_proc_address);

};

/**
 *  VDPAU main class
 */
class CDecoder
 : public CDVDVideoCodecFFmpeg::IHardwareDecoder
#if HAS_GL
 , public IDispResource
#endif
{
   friend class CVdpauRenderPicture;

public:

  struct Desc
  {
    const char *name;
    uint32_t id;
    uint32_t aux; /* optional extra parameter... */
  };

  CDecoder();
  virtual ~CDecoder();

  virtual bool Open      (AVCodecContext* avctx, AVCodecContext* mainctx, const enum AVPixelFormat, unsigned int surfaces = 0);
  virtual int  Decode    (AVCodecContext* avctx, AVFrame* frame);
  virtual bool GetPicture(AVCodecContext* avctx, AVFrame* frame, DVDVideoPicture* picture);
  virtual void Reset();
  virtual void Close();
  virtual long Release();
  virtual bool CanSkipDeint();
  virtual unsigned GetAllowedReferences() { return 5; }

  virtual int  Check(AVCodecContext* avctx);
  virtual const std::string Name() { return "vdpau"; }
  virtual void SetCodecControl(int flags);

  bool Supports(VdpVideoMixerFeature feature);
  bool Supports(EINTERLACEMETHOD method);
  EINTERLACEMETHOD AutoInterlaceMethod();
  static bool IsVDPAUFormat(AVPixelFormat fmt);

  static void FFReleaseBuffer(void *opaque, uint8_t *data);
  static int FFGetBuffer(AVCodecContext *avctx, AVFrame *pic, int flags);
  static int Render(struct AVCodecContext *s, struct AVFrame *src,
                    const VdpPictureInfo *info, uint32_t buffers_used,
                    const VdpBitstreamBuffer *buffers);
  static int FFSetVideoHeader(AVCodecContext *avctx, uint32_t id);

  virtual void OnLostDisplay();
  virtual void OnResetDisplay();
  void Present(uint32_t surface);
  
protected:
  void SetWidthHeight(int width, int height);
  bool ConfigVDPAU(AVCodecContext *avctx, int ref_frames);
  bool CheckStatus(VdpStatus vdp_st, int line);
  void FiniVDPAUOutput();
  void ReturnRenderPicture(CVdpauRenderPicture *renderPic);
  long ReleasePicReference();

  static bool ReadFormatOf( AVCodecID codec
                          , VdpDecoderProfile &decoder_profile
                          , VdpChromaType     &chroma_type);

  // OnLostDevice triggers transition from all states to LOST
  // internal errors trigger transition from OPEN to RESET
  // OnResetDevice triggers transition from LOST to RESET
  enum EDisplayState
  { VDPAU_OPEN
  , VDPAU_RESET
  , VDPAU_LOST
  , VDPAU_ERROR
  } m_DisplayState;
  CCriticalSection m_DecoderSection;
  CEvent         m_DisplayEvent;
  int m_ErrorCount;

  ThreadIdentifier m_decoderThread;
  bool          m_vdpauConfigured;
  CVdpauConfig  m_vdpauConfig;
  CVideoSurfaces m_videoSurfaces;
  AVVDPAUContext m_hwContext;
  VdpDecoderControlData m_data;
  uint32_t     m_dataId;
  bool         m_dataSet;

  COutput       m_vdpauOutput;
  CVdpauBufferStats m_bufferStats;
  CEvent        m_inMsgEvent;
  CVdpauRenderPicture *m_presentPicture;

  int m_codecControl;
};

}

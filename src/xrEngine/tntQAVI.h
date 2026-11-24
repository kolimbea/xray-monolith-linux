#ifndef tntQAVIH
#define tntQAVIH
#ifdef __linux__
// Video-for-Windows AVI textures are not available on Linux yet: Load() fails and the texture stays
// on its fallback, as on Windows when an .avi cannot be opened. (Theora .ogm textures are separate.)
class ENGINE_API CAviPlayerCustom
{
public:
	DWORD m_dwWidth = 0, m_dwHeight = 0;
	CAviPlayerCustom() = default;
	~CAviPlayerCustom() = default;
	VOID GetSize(DWORD* w, DWORD* h) { if (w) *w = m_dwWidth; if (h) *h = m_dwHeight; }
	BOOL Load(char*) { return FALSE; }
	BOOL GetFrame(BYTE**) { return FALSE; }
	BOOL NeedUpdate() { return FALSE; }
	INT SetSpeed(INT) { return 100; }
};
#else


#include <math.h>

//#include "tntTimer.h"
#include "vfw.h"
#include "mmsystem.h"

// replaced with standard AVIIF_KEYFRAME
//rr #define AVIINDEX_ISKEYFRAME 0x10 // ключевой кадр

// reverse enginered AVI index v.1 format
/*struct AviIndex {

 DWORD dwChunkType; // chunk type, i.e. '##dc' - DIB compressed
 DWORD dwFlags; // key-frame etc.
 DWORD dwOffset; // sub-chunk offset from the begining of the LIST chunk
 DWORD dwLenght; // chunk lenght

 };

 typedef struct {
 FOURCC fccType;
 FOURCC fccHandler;
 DWORD dwFlags;
 DWORD dwPriority;
 DWORD dwInitialFrames;
 DWORD dwScale;
 DWORD dwRate;
 DWORD dwStart;
 DWORD dwLength;
 DWORD dwSuggestedBufferSize;
 DWORD dwQuality;
 DWORD dwSampleSize;
 RECT rcFrame;
 } AVIStreamHeader;
 */
typedef struct
{
	FOURCC fccType;
	FOURCC fccHandler;
	DWORD dwFlags;
	DWORD dwPriority;
	DWORD dwInitialFrames;
	DWORD dwScale;
	DWORD dwRate;
	DWORD dwStart;
	DWORD dwLength;
	DWORD dwSuggestedBufferSize;
	DWORD dwQuality;
	DWORD dwSampleSize;

	struct
	{
		WORD left;
		WORD top;
		WORD right;
		WORD bottom;
	};

	// RECT rcFrame; - лажа в MSDN
} AVIStreamHeaderCustom;

class ENGINE_API CAviPlayerCustom
{
protected:
	CAviPlayerCustom* alpha;
protected:
	AVIINDEXENTRY* m_pMovieIndex;
	BYTE* m_pMovieData;
	HIC m_aviIC;
	BYTE* m_pDecompressedBuf;

	BITMAPINFOHEADER m_biOutFormat;
	BITMAPINFOHEADER m_biInFormat;

	float m_fRate; // стандартная скорость, fps
	float m_fCurrentRate; // текущая скорость, fps

	DWORD m_dwFrameTotal;
	DWORD m_dwFrameCurrent;
	u32 m_dwFirstFrameOffset;


	DWORD CalcFrame();

	BOOL DecompressFrame(DWORD dwFrameNum);
	VOID PreRoll(DWORD dwFrameNum);

public:
	CAviPlayerCustom();
	~CAviPlayerCustom();

	DWORD m_dwWidth, m_dwHeight;

	VOID GetSize(DWORD* dwWidth, DWORD* dwHeight);

	BOOL Load(char* fname);
	BOOL GetFrame(BYTE** pDest);

	BOOL NeedUpdate() { return CalcFrame() != m_dwFrameCurrent; }
	INT SetSpeed(INT nPercent);
};
#endif // __linux__
#endif

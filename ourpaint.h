/*
* Our Paint: A light weight GPU powered painting program.
* Copyright (C) 2022-2023 Wu Yiming
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "la_5.h"

#ifndef OURPAINT_GIT_BRANCH
#define OURPAINT_GIT_BRANCH "Release 1"
#endif

// No need to show hash when not compiled from git repo.
//#ifndef OURPAINT_GIT_HASH
//#define OURPAINT_GIT_HASH "?"
//#endif

extern unsigned char DATA_SPLASH[];
extern unsigned char DATA_SPLASH_HIGHDPI[];

#ifdef __cplusplus
extern "C" {
#endif
extern const char OUR_CANVAS_SHADER[];
extern const char OUR_COMPOSITION_SHADER[];
#ifdef __cplusplus
} // extern "C"
#endif

#define OUR_AT_CROP_CENTER 0
#define OUR_AT_CROP_L 1
#define OUR_AT_CROP_R 2
#define OUR_AT_CROP_U 3
#define OUR_AT_CROP_B 4
#define OUR_AT_CROP_UL 5
#define OUR_AT_CROP_UR 6
#define OUR_AT_CROP_BL 7
#define OUR_AT_CROP_BR 8

#define OUR_VERSION_MAJOR 0
#define OUR_VERSION_MINOR 1

STRUCTURE(OurCanvasDraw){
    laCanvasExtra Base;
    int HideBrushCircle;
    int AtCrop;
    real CanvasLastX,CanvasLastY;
    real CanvasDownX,CanvasDownY;
    real LastPressure;
    real LastTilt[2];
};


#define OUR_TILE_W 1024
#define OUR_TILES_PER_ROW 100
#define OUR_TILE_CTR (OUR_TILES_PER_ROW/2)
#define OUR_TILE_SEAM 12
#define OUR_TILE_W_USE (OUR_TILE_W-OUR_TILE_SEAM*2)

STRUCTURE(OurTexTile){
    tnsTexture* Texture;
    uint16_t* Data;
    int l,r,u,b;
    uint16_t* FullData;
    uint16_t* CopyBuffer;
    int cl,cr,cu,cb;
};

#define OUR_BLEND_NORMAL 0
#define OUR_BLEND_ADD 1

STRUCTURE(OurLayer){
    laListItem Item;
    laSafeString Name;
    int OffsetX,OffsetY;
    real Transparency;
    int Lock;
    int Hide;
    int BlendMode;
    OurTexTile** TexTiles[OUR_TILES_PER_ROW];
};

STRUCTURE(OurLayerWrite){
    unsigned char* data;
    size_t NextData, MaxData;
};

STRUCTURE(OurLayerRead){
    unsigned char* data;
    size_t NextData;
};

STRUCTURE(OurBrushSettingsNode){
    laBaseNode Base;
    laNodeOutSocket* CanvasScale;  real rCanvasScale;
    laNodeOutSocket* Size;         real rSize;
    laNodeOutSocket* Transparency; real rTransparency;
    laNodeOutSocket* Hardness;     real rHardness;
    laNodeOutSocket* Smudge;       real rSmudge;
    laNodeOutSocket* DabsPerSize;  real rDabsPerSize;
    laNodeOutSocket* SmudgeLength; real rSmudgeLength;
    laNodeOutSocket* Slender;      real rSlender;
    laNodeOutSocket* Angle;        real rAngle;
    laNodeOutSocket* Color;
};
STRUCTURE(OurBrushOutputsNode){
    laBaseNode Base;
    laNodeInSocket* Offset;
    laNodeInSocket* Size;
    laNodeInSocket* Transparency;
    laNodeInSocket* Hardness;
    laNodeInSocket* Smudge;
    laNodeInSocket* DabsPerSize;
    laNodeInSocket* SmudgeLength;
    laNodeInSocket* Slender;
    laNodeInSocket* Angle;
    laNodeInSocket* Color;
};
STRUCTURE(OurBrushDeviceNode){
    laBaseNode Base;
    laNodeOutSocket* Pressure; real rPressure;
    laNodeOutSocket* Position; real rPosition[2];
    laNodeOutSocket* Tilt;     real rTilt[2];
    laNodeOutSocket* IsEraser; int  rIsEraser;
    laNodeOutSocket* Speed;    real rSpeed;
    laNodeOutSocket* Angle;    real rAngle;
    laNodeOutSocket* Length;   real rLength;
    laNodeOutSocket* LengthAccum; real rLengthAccum;
};

STRUCTURE(OurBrush){
    laListItem Item;
    laSafeString Name;
    real Size;
    real DabsPerSize;
    real Hardness;
    real Transparency;
    real Smudge;
    real SmudgeResampleLength; real SmudgeAccum; int SmudgeRestart; real BrushRemainingDist;
    real Slender;
    real Angle;
    real Smoothness;
    real MaxStrokeLength;
    int PressureSize,PressureHardness,PressureTransparency,PressureSmudge; // the simple way

    int Binding,DefaultAsEraser;

    int UseNodes; // the flexible way
    laRackPage* Rack;

    real LastX,LastY,LastAngle;
    
    real EvalColor[3];
    real EvalOffset[2];
    real EvalSize;
    real EvalDabsPerSize;
    real EvalHardness;
    real EvalTransparency;
    real EvalSmudge;
    real EvalSmudgeLength;
    real EvalSlender;
    real EvalAngle;

    real EvalSpeed;
    real EvalStrokeLength;
    real EvalStrokeLengthAccum;
    real EvalPressure;
    real EvalPosition[2];
    real EvalTilt[2];
    real EvalStrokeAngle;
    int  EvalIsEraser;
};
STRUCTURE(OurDab){
    float X,Y;
    float Size;
    float Hardness;
    float Smudge; int ResampleSmudge;
    float Color[4];
    float Slender;
    float Angle;
    float Recentness;
};

STRUCTURE(OurUndoTile){
    laListItem Item;
    int col,row;
    uint16_t* CopyData;
    int l,r,u,b;
};
STRUCTURE(OurUndo){
    OurLayer* Layer;
    laListHandle Tiles;
};

#define OUR_TOOL_PAINT 0
#define OUR_TOOL_CROP 1

#define OUR_PNG_READ_INPUT_FLAT 0
#define OUR_PNG_READ_INPUT_ICC  1
#define OUR_PNG_READ_INPUT_SRGB 2
#define OUR_PNG_READ_INPUT_LINEAR_SRGB 3
#define OUR_PNG_READ_INPUT_CLAY 4
#define OUR_PNG_READ_INPUT_LINEAR_CLAY 5

#define OUR_PNG_READ_OUTPUT_CANVAS 0
#define OUR_PNG_READ_OUTPUT_LINEAR_SRGB OUR_PNG_READ_INPUT_LINEAR_SRGB
#define OUR_PNG_READ_OUTPUT_LINEAR_CLAY OUR_PNG_READ_INPUT_LINEAR_CLAY

#define OUR_CANVAS_INTERPRETATION_SRGB 0
#define OUR_CANVAS_INTERPRETATION_CLAY 1

#define OUR_EXPORT_BIT_DEPTH_8  0
#define OUR_EXPORT_BIT_DEPTH_16 1

#define OUR_EXPORT_COLOR_MODE_SRGB 0
#define OUR_EXPORT_COLOR_MODE_CLAY 1
#define OUR_EXPORT_COLOR_MODE_FLAT 2

STRUCTURE(OurPNGReadExtra){
    int Confirming;
    laSafeString* FilePath;
    laSafeString* iccName;
    int HassRGB;
    int HasProfile;
    int InputMode;
    int OutputMode;
};
STRUCTURE(OurPNGWriteExtra){
    int Confirming;
    laSafeString* FilePath;
    int BitDepth;
    int ColorProfile;
};

STRUCTURE(OurPaint){
    real pad;

    laListHandle CanvasSaverDummyList;
    laProp*      CanvasSaverDummyProp;

    laListHandle BadEvents;

    tnsImage* SplashImage;
    tnsImage* SplashImageHigh;

    laListHandle Layers;
    OurLayer*    CurrentLayer;
    laListHandle Brushes;
    OurBrush*    CurrentBrush;
    real SaveBrushSize,SaveEraserSize;
    OurDab* Dabs; int NextDab,MaxDab;
    laListHandle BrushEval;

    real CurrentScale;
    real DefaultScale;

    int Tool,ActiveTool,Erasing,EventErasing;
    int LockBackground;
    int PenID,EraserID;
    int X,Y,W,H; //border
    int ColorInterpretation;
    int ShowBorder,UseBorder;
    int ShowTiles;
    int AllowNonPressure,PaintProcessedEvents;
    int BadEventsLimit,BadEventCount,BadEventsGiveUp;

    int LockRadius;
    int EnableBrushCircle;
    int DefaultBitDepth;
    int DefaultColorProfile;
    int PaintUndoLimit;
    int SpectralMode;

    tnsTexture* SmudgeTexture;
    GLuint CanvasShader;      GLuint CanvasProgram;
    GLuint CompositionShader; GLuint CompositionProgram;
    GLint uBrushCorner;
    GLint uBrushCenter;
    GLint uBrushSize;
    GLint uBrushHardness;
    GLint uBrushSmudge;
    GLint uBrushRecentness;
    GLint uBrushColor;
    GLint uBrushSlender;
    GLint uBrushAngle;
    GLint uBrushRoutineSelection;
    GLint uMixRoutineSelection;
    GLint uBrushErasing;
    GLint RoutineDoDabs;
    GLint RoutineDoSample;
    GLint RoutineDoMixNormal;
    GLint RoutineDoMixSpectral;
    GLint uBlendMode;
    GLint uAlphaTop;
    GLint uAlphaBottom;

    real CurrentColor[3];
    real BackgroundColor[3];
    uint16_t BColorU16[4];
    real BorderAlpha;

    real xmin,xmax,ymin,ymax; // stroke bbox for undo region
    int ResetBrush;

    uint16_t *ImageBuffer;
    int ImageW,ImageH,ImageX,ImageY,LoadX,LoadY,TempLoadX,TempLoadY;

    void* icc_LinearsRGB; int iccsize_LinearsRGB;
    void* icc_LinearClay; int iccsize_LinearClay;
    void* icc_sRGB; int iccsize_sRGB;
    void* icc_Clay; int iccsize_Clay;
};

int ourInit();
void ourRegisterNodes();
int ourRebuildBrushEval();
int ourEvalBrush();
void ourMakeTranslations();
void our_EnableSplashPanel();


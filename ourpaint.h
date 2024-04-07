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
#define OUR_VERSION_MINOR 2
#define OUR_VERSION_SUB 1

#define OUR_PAINT_NAME_STRING "Our Paint v0.2"

#define OUR_SIGNAL_PICK 1
#define OUR_SIGNAL_MOVE 2
#define OUR_SIGNAL_PICK 3
#define OUR_SIGNAL_TOGGLE_ERASING 4
#define OUR_SIGNAL_ZOOM_IN 5
#define OUR_SIGNAL_ZOOM_OUT 6
#define OUR_SIGNAL_BRUSH_BIGGER 7
#define OUR_SIGNAL_BRUSH_SMALLER 8
#define OUR_SIGNAL_TOGGLE_SKETCH 9

STRUCTURE(OurCanvasDraw){
    laCanvasExtra Base;
    int HideBrushCircle;
    int AtCrop;
    real CanvasLastX,CanvasLastY;
    real CanvasDownX,CanvasDownY;
    real PointerX,PointerY,DownTilt;
    real LastPressure;
    real LastTilt[2];
    real LastTwist;
    int MovedX,MovedY;
};

#define OUR_DPC (600*0.3937007874)

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
    laSafeString* Name;
    int OffsetX,OffsetY;
    real Transparency;
    int Lock;
    int Hide;
    int BlendMode;
    int AsSketch;
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
    laNodeOutSocket* Gunkyness;    real rGunkyness;
    laNodeOutSocket* Force;        real rForce;
    laNodeOutSocket* Color;
    laNodeOutSocket* Iteration;    int  rIteration;
    laNodeOutSocket* Custom1;      real rCustom1;
    laNodeOutSocket* Custom2;      real rCustom2;
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
    laNodeInSocket* Gunkyness;
    laNodeInSocket* Force;
    laNodeInSocket* Repeats;
    laNodeInSocket* Discard;
};
STRUCTURE(OurBrushDeviceNode){
    laBaseNode Base;
    laNodeOutSocket* Pressure; real rPressure;
    laNodeOutSocket* Position; real rPosition[2];
    laNodeOutSocket* Tilt;     real rTilt[2];
    laNodeOutSocket* Twist;    real rTwist;
    laNodeOutSocket* IsEraser; int  rIsEraser;
    laNodeOutSocket* Speed;    real rSpeed;
    laNodeOutSocket* Angle;    real rAngle;
    laNodeOutSocket* Length;   real rLength;
    laNodeOutSocket* LengthAccum; real rLengthAccum;
};

STRUCTURE(OurBrush){
    laListItem Item;
    laSafeString* Name;
    real Size;
    real DabsPerSize;
    real Hardness;
    real Transparency;
    real Smudge;
    real SmudgeResampleLength; real SmudgeAccum; int SmudgeRestart; real BrushRemainingDist;
    real Slender;
    real Angle;
    real Force, Gunkyness;
    real Smoothness;
    real MaxStrokeLength;
    real Custom1,Custom2; laSafeString *Custom1Name,*Custom2Name;
    int Iteration;
    int PressureSize,PressureHardness,PressureTransparency,PressureSmudge,PressureForce,TwistAngle; // the simple way

    int Binding,DefaultAsEraser;
    int ShowInPages;

    real VisualOffset;
    real VisualOffsetAngle;

    int16_t OffsetFollowPenTilt;
    int16_t UseNodes; // the flexible way
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
    real EvalForce, EvalGunkyness;

    real EvalSpeed;
    real EvalStrokeLength;
    real EvalStrokeLengthAccum;
    real EvalPressure;
    real EvalPosition[2];
    real EvalTilt[2];
    real EvalTwist;
    real EvalStrokeAngle;
    int  EvalIsEraser;

    int EvalRepeats;
    int EvalDiscard;
};
STRUCTURE(OurDab){
    float X,Y;
    float Size;
    float Hardness;
    float Smudge; int ResampleSmudge;
    float Color[4];
    float Slender;
    float Angle;
    float Direction[2];
    float Force;
    float Gunkyness;
    float Recentness;
};

NEED_STRUCTURE(OurColorPallette);
STRUCTURE(OurColorItem){
    laListItem Item;
    tnsVector3d Color;
    OurColorPallette* Parent;
};
STRUCTURE(OurColorPallette){
    laListItem Item;
    laSafeString* Name;
    laListHandle Colors;
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
STRUCTURE(OurMoveUndo){
    OurLayer* Layer;
    int dx,dy;
};

#define OUR_TOOL_PAINT 0
#define OUR_TOOL_CROP 1
#define OUR_TOOL_MOVE 2

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

#define OUR_BRUSH_PAGE_LIST 128

STRUCTURE(OurPNGReadExtra){
    int Confirming;
    laSafeString* FilePath;
    laSafeString* iccName;
    int HassRGB;
    int HasProfile;
    int InputMode;
    int OutputMode;
    int Offsets[2];
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

    laListHandle Pallettes;
    OurColorPallette* CurrentPallette;

    laListHandle Layers;
    OurLayer*    CurrentLayer;
    laListHandle Brushes;
    OurBrush*    CurrentBrush;
    real SaveBrushSize,SaveEraserSize;
    OurDab* Dabs; int NextDab,MaxDab;
    float LastBrushCenter[2];
    int CanvasVersion;

    laSafeString* Notes;

    real Smoothness;
    real LastX, LastY;

    real CurrentScale;
    real DefaultScale;

    int BrushPage;
    int Tool,ActiveTool,Erasing,EventErasing;
    int LockBackground;
    int BackgroundType;
    int BackgroundRandom;
    real BackgroundFactor;
    int PenID,EraserID;
    int X,Y,W,H; //border
    int ColorInterpretation;
    int ShowBorder,UseBorder;
    int ShowTiles;
    int AllowNonPressure,PaintProcessedEvents;
    int BadEventsLimit,BadEventCount,BadEventsGiveUp;

    int LockRadius;
    int EnableBrushCircle,ShowBrushName; int EventHasTwist; real EventTwistAngle;
    int DefaultBitDepth;
    int DefaultColorProfile;
    int PaintUndoLimit;
    int SpectralMode;
    int SketchMode;

    tnsTexture* SmudgeTexture;
    GLuint CanvasShader;      GLuint CanvasProgram;
    GLuint CompositionShader; GLuint CompositionProgram;
    GLint uCanvasType;
    GLint uCanvasRandom;
    GLint uCanvasFactor;
    GLint uImageOffset;
    GLint uBrushCorner;
    GLint uBrushCenter;
    GLint uBrushSize;
    GLint uBrushHardness;
    GLint uBrushSmudge;
    GLint uBrushRecentness;
    GLint uBrushColor;
    GLint uBrushSlender;
    GLint uBrushAngle;
    GLint uBrushDirection;
    GLint uBrushForce;
    GLint uBrushGunkyness;
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
    uint8_t  BColorU8[4];
    real BorderAlpha;

    int ShowStripes;
    int ShowRef;
    int RefSize;
    int RefCategory;
    int RefOrientation;
    real RefMargins[3],RefPaddings[2];
    int RefBiases[2];
    real RefAlpha;

    real xmin,xmax,ymin,ymax; // stroke bbox for undo region
    int ResetBrush;
    
    int SaveFailed;

    uint16_t *ImageBuffer;
    int ImageW,ImageH,ImageX,ImageY,LoadX,LoadY,TempLoadX,TempLoadY;

    uint8_t* ThumbnailBuffer;

    void* icc_LinearsRGB; int iccsize_LinearsRGB;
    void* icc_LinearClay; int iccsize_LinearClay;
    void* icc_sRGB; int iccsize_sRGB;
    void* icc_Clay; int iccsize_Clay;
};


int ourProcessInitArgs(int argc, char* argv[]);

int ourInit();
void ourRegisterNodes();
int ourRebuildBrushEval();
int ourEvalBrush();
void ourMakeTranslations();
void our_EnableSplashPanel();


#include "la_5.h"

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
STRUCTURE(OurLayer){
    laListItem Item;
    laSafeString Name;
    int OffsetX,OffsetY;
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

STRUCTURE(OurPaint){
    real pad;

    laListHandle CanvasSaverDummyList;
    laProp*      CanvasSaverDummyProp;

    laListHandle Layers;
    OurLayer*    CurrentLayer;
    laListHandle Brushes;
    OurBrush*    CurrentBrush;
    OurDab* Dabs; int NextDab,MaxDab;
    laListHandle BrushEval;

    real CurrentScale;

    int Tool,ActiveTool,Erasing,EventErasing;
    int PenID,EraserID;
    int X,Y,W,H; //border
    int ShowBorder,UseBorder;
    int ShowTiles;

    int LockRadius;
    int EnableBrushCircle;

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
    GLint uBrushErasing;
    GLint RoutineDoDabs;
    GLint RoutineDoSample;
    GLint uMode;

    real CurrentColor[3];
    real BackgroundColor[3];
    uint16_t BColorU16[4];
    real BorderAlpha;

    real xmin,xmax,ymin,ymax; // stroke bbox for undo region
    int ResetBrush;

    uint16_t *ImageBuffer;
    int ImageW,ImageH,ImageX,ImageY,LoadX,LoadY,TempLoadX,TempLoadY;

    void* icc_LinearsRGB; int iccsize_LinearsRGB;
    void* icc_sRGB; int iccsize_sRGB;
};

int ourInit();
void ourRegisterNodes();
int ourRebuildBrushEval();
int ourEvalBrush();


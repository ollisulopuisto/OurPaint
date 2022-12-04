#include "la_5.h"

STRUCTURE(OurCanvasDraw){
    laCanvasExtra Base;
    int ShowTiles;
    real CanvasLastX,CanvasLastY;
    real LastPressure;
};


#define OUR_TILE_W 64
#define OUR_TEX_TILE_W 1024
#define OUR_TEX_TILES_PER_ROW 100
#define OUR_TILES_PER_ROW (OUR_TEX_TILES_PER_ROW*(OUR_TEX_TILE_W/OUR_TILE_W))
#define OUR_TEX_TILE_CTR (OUR_TEX_TILES_PER_ROW/2)
#define OUR_TEX_TILE_SEAM 12
#define OUR_TEX_TILE_W_USE (OUR_TEX_TILE_W-OUR_TEX_TILE_SEAM*2)

STRUCTURE(OurTile){
    int X,Y; // with offset so not neccessarily n*OUR_TILE_W
    void* Data;
};
STRUCTURE(OurTexTile){
    tnsTexture* Texture;
};
STRUCTURE(OurLayer){
    laListItem Item;
    laSafeString Name;
    int OffsetX,OffsetY;
    OurTexTile** TexTiles[OUR_TEX_TILES_PER_ROW];
};

STRUCTURE(OurBrush){
    laListItem Item;
    laSafeString Name;
    real Size;
    real DabsPerSize;
    real Hardness;
    real Transparency;
    real Smudge;
    real SmudgeResampleLength; real SmudgeAccum;
    real BrushRemainingDist;
    int UseNodes; // the flexible way
    int PressureSize,PressureHardness,PressureTransparency,PressureSmudge; // the simple way
};
STRUCTURE(OurDab){
    float X,Y;
    float Size;
    float Hardness;
    float Smudge; int ResampleSmudge;
    float Color[4];
};

STRUCTURE(OurPaint){
    real pad;

    laListHandle Layers;
    OurLayer*    CurrentLayer;

    laListHandle Brushes;
    OurBrush*    CurrentBrush;
    OurDab* Dabs; int NextDab,MaxDab;

    tnsTexture* SmudgeTexture;
    GLuint CanvasShader;
    GLuint CanvasProgram;
    GLint uBrushCorner;
    GLint uBrushCenter;
    GLint uBrushSize;
    GLint uBrushHardness;
    GLint uBrushSmudge;
    GLint uBrushColor;
    GLint uBrushRoutineSelection;
    GLint RoutineDoDabs;
    GLint RoutineDoSample;

    real CurrentColor[4];
    real BackgroundColor[3];
};

void ourInit();


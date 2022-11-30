#include "la_5.h"

STRUCTURE(OurCanvasDraw){
    laCanvasExtra Base;
    int ShowTiles;
    real CanvasLastX,CanvasLastY;
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
};
STRUCTURE(OurDab){
    float X,Y;
    float Size;
    float Hardness;
    float Smudge;
    float Color[4];
};

STRUCTURE(OurPaint){
    real pad;

    laListHandle Layers;
    OurLayer*    CurrentLayer;

    laListHandle Brushes;
    OurBrush*    CurrentBrush;
    real BrushRemainingDist;
    OurDab* Dabs; int NextDab,MaxDab;

    tnsTexture* Content;
    GLuint CanvasShader;
    GLuint CanvasProgram;
    GLint uBrushCorner;
    GLint uBrushCenter;
    GLint uBrushSize;
    GLint uBrushHardness;
    GLint uBrushSmudge;
    GLint uBrushColor;

    real CurrentColor[4];
};

void ourInit();


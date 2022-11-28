#include "la_5.h"

STRUCTURE(OurCanvasDraw){
    laCanvasExtra Base;
};


STRUCTURE(OurRow){
    laListItem Item;
    laListHandle Tiles;
    int Y;
};
STRUCTURE(OurTile){
    laListItem Item;
    OurRow Row;
    void* Data;
    int X; 
};
STRUCTURE(OurTexRow){
    laListItem Item;
    laListHandle Tiles;
    int Y;
};
STRUCTURE(OurTexTile){
    laListItem Item;
    OurRow Row;
    tnsTexture* Texture;
    int X;
};

STRUCTURE(OurLayer){
    laListItem Item;
    laSafeString Name;
    int OffsetX,OffsetY;
    laListHandle Rows;
    laListHandle TextureRows;
};

STRUCTURE(OurPaint){
    real pad;

    laListHandle Layers;

    tnsTexture* Content;
    GLuint CanvasShader;
    GLuint CanvasProgram;
    GLint CanvasTaskUniform;
};

void ourInit();


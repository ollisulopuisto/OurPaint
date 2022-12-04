#include "ourpaint.h"

OurPaint *Our;
extern LA MAIN;
extern tnsMain* T;

const char OUR_CANVAS_SHADER[]="#version 430\n\
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;\n\
layout(rgba8, binding = 0) uniform image2D img;\n\
layout(rgba8, binding = 1) coherent uniform image2D smudge_buckets;\n\
uniform ivec2 uBrushCorner;\n\
uniform vec2 uBrushCenter;\n\
uniform float uBrushSize;\n\
uniform float uBrushHardness;\n\
uniform float uBrushSmudge;\n\
uniform vec4 uBrushColor;\n\
uniform vec4 uBackgroundColor;\n\
int dab(float d, vec4 color, float size, float hardness, float smudge, vec4 smudge_color, vec4 last_color, out vec4 final){\n\
    color.rgb=mix(color,smudge_color,smudge*smudge_color.a).rgb;\n\
    color.a=color.a*(1-smudge);\n\
    float a=clamp(color.a*1-pow(d/size,1+1/(1-hardness)),0,1);\n\
    final=vec4(mix(last_color.rgb,color.rgb,a), 1-(1-last_color.a)*(1-a));\n\
    return 1;\n\
}\n\
subroutine void BrushRoutines();\n\
subroutine(BrushRoutines) void DoDabs(){\n\
    ivec2 px = ivec2(gl_GlobalInvocationID.xy)+uBrushCorner;\n\
    float dd=distance(vec2(px),uBrushCenter); if(dd>uBrushSize) return;\n\
    vec4 final;\n\
    vec4 dabc=imageLoad(img, px);\n\
    vec4 smudgec=imageLoad(smudge_buckets,ivec2(0,0));\n\
    dab(dd,uBrushColor,uBrushSize,uBrushHardness,uBrushSmudge,smudgec,dabc,final);\n\
    dabc=final;\n\
    imageStore(img, px, dabc);\n\
}\n\
subroutine(BrushRoutines) void DoSample(){\n\
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);\n\
    if(p!=ivec2(0,0)) return;\n\
    ivec2 px=p+uBrushCorner;\n\
    vec4 color=imageLoad(img, px);\n\
    imageStore(smudge_buckets,ivec2(0,0),color);\n\
}\n\
subroutine uniform BrushRoutines uBrushRoutineSelection;\n\
\n\
void main() {\n\
    uBrushRoutineSelection();\n\
}\n\
";


void ourui_CanvasPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laUiItem* ui=laShowCanvas(uil,c,0,"our.canvas",0,-1);
}
void ourui_Layer(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.7); cl=laLeftColumn(c,0);cr=laRightColumn(c,1);
    laUiItem* b=laBeginRow(uil,cl,0,0);
    laShowHeightAdjuster(uil,cl,This,"__move",0);
    laShowItemFull(uil,cl,This,"name",LA_WIDGET_STRING_PLAIN,0,0,0)->Expand=1;
    laEndRow(uil,b);
    laUiItem* b1=laOnConditionToggle(uil,cr,0,0,0,0,0);{ strSafeSet(&b1->ExtraInstructions,"text=☰");
        b=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,This,"remove")->Flags|=LA_UI_FLAGS_ICON;
        laShowSeparator(uil,c)->Expand=1;
        laShowItemFull(uil,c,This,"move",0,"direction=up;icon=🡱;",0,0)->Flags|=LA_UI_FLAGS_ICON;
        laShowItemFull(uil,c,This,"move",0,"direction=down;icon=🡳;",0,0)->Flags|=LA_UI_FLAGS_ICON;
        laEndRow(uil,b);
    }laEndCondition(uil,b1);
}
void ourui_LayersPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);

    laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.canvas.current_layer"));{
        laUiItem* b1=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,0,"our.canvas.current_layer.name")->Expand=1;
        laShowItem(uil,c,0,"OUR_new_layer")->Flags|=LA_UI_FLAGS_ICON;
        laEndRow(uil,b1);
    }laElse(uil,b);{
        laShowItem(uil,c,0,"OUR_new_layer");
    }laEndCondition(uil,b);

    laShowItemFull(uil,c,0,"our.canvas.layers",0,0,0,0);

    b=laBeginRow(uil,c,0,0);
    laShowLabel(uil,c,"Background",0,0)->Expand=1;
    laShowItemFull(uil,c,0,"our.background_color",LA_WIDGET_FLOAT_COLOR,0,0,0);
    laEndRow(uil,b);
}
void ourui_Brush(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.7); cl=laLeftColumn(c,0);cr=laRightColumn(c,1);
    laUiItem* b=laBeginRow(uil,cl,0,0);
    laShowHeightAdjuster(uil,cl,This,"__move",0);
    laShowItemFull(uil,cl,This,"name",LA_WIDGET_STRING_PLAIN,0,0,0)->Expand=1;
    laEndRow(uil,b);
    laUiItem* b1=laOnConditionToggle(uil,cr,0,0,0,0,0);{ strSafeSet(&b1->ExtraInstructions,"text=☰");
        b=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,This,"remove")->Flags|=LA_UI_FLAGS_ICON;
        laShowSeparator(uil,c)->Expand=1;
        laShowItemFull(uil,c,This,"move",0,"direction=up;icon=🡱;",0,0)->Flags|=LA_UI_FLAGS_ICON;
        laShowItemFull(uil,c,This,"move",0,"direction=down;icon=🡳;",0,0)->Flags|=LA_UI_FLAGS_ICON;
        laEndRow(uil,b);
    }laEndCondition(uil,b1);
}
void ourui_BrushesPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laUiItem* b1;
#define OUR_BR b1=laBeginRow(uil,c,0,0);
#define OUR_ER laEndRow(uil,b1);

    laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.current_brush"));{
        laShowItem(uil,c,0,"our.current_brush.name");
        OUR_BR laShowItem(uil,c,0,"our.current_brush.size")->Expand=1; laShowItemFull(uil,c,0,"our.current_brush.pressure_size",0,"text=P",0,0); OUR_ER
        OUR_BR laShowItem(uil,c,0,"our.current_brush.transparency")->Expand=1;  laShowItemFull(uil,c,0,"our.current_brush.pressure_transparency",0,"text=P",0,0); OUR_ER
        OUR_BR laShowItem(uil,c,0,"our.current_brush.hardness")->Expand=1;  laShowItemFull(uil,c,0,"our.current_brush.pressure_hardness",0,"text=P",0,0); OUR_ER
        OUR_BR laShowItem(uil,c,0,"our.current_brush.smudge")->Expand=1; laShowItemFull(uil,c,0,"our.current_brush.pressure_smudge",0,"text=P",0,0); OUR_ER
        laShowItem(uil,c,0,"our.current_brush.dabs_per_size");
        laShowItem(uil,c,0,"our.current_brush.smudge_resample_length");
    }laEndCondition(uil,b);

    laShowLabel(uil,c,"Select a brush:",0,0);

    laShowItemFull(uil,c,0,"our.brushes",0,0,0,0);
    laShowItem(uil,c,0,"OUR_new_brush");
}
void ourui_ColorPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    
    laShowItemFull(uil,c,0,"our.current_color",LA_WIDGET_FLOAT_COLOR_HCY,0,0,0);
}


void our_CanvasDrawTextures(){
    tnsUseImmShader; tnsEnableShaderv(T->immShader); tnsUniformUseTexture(T->immShader,0,0); tnsUseNoTexture();
    for(OurLayer* l=Our->Layers.pLast;l;l=l->Item.pPrev){
        int any=0;
        for(int row=0;row<OUR_TEX_TILES_PER_ROW;row++){
            if(!l->TexTiles[row]) continue;
            for(int col=0;col<OUR_TEX_TILES_PER_ROW;col++){
                if(!l->TexTiles[row][col]) continue;
                int sx=((real)col-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM,sy=((real)row-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM;            
                tnsDraw2DTextureDirectly(l->TexTiles[row][col]->Texture,sx,sy+OUR_TEX_TILE_W,OUR_TEX_TILE_W,-OUR_TEX_TILE_W);
                any=1;
            }
        }
        if(any) tnsFlush();
    }
}

void our_CanvasDrawTiles(){
    OurLayer* l=Our->CurrentLayer; if(!l) return;
    tnsUseImmShader; tnsEnableShaderv(T->immShader); tnsUniformUseTexture(T->immShader,0,0); tnsUseNoTexture();
    int any=0;
    for(int row=0;row<OUR_TEX_TILES_PER_ROW;row++){
        if(!l->TexTiles[row]) continue;
        for(int col=0;col<OUR_TEX_TILES_PER_ROW;col++){
            if(!l->TexTiles[row][col]) continue;
            int sx=((real)col-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM,sy=((real)row-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM;            
            tnsVertex2d(sx, sy); tnsVertex2d(sx+OUR_TEX_TILE_W,sy);
            tnsVertex2d(sx+OUR_TEX_TILE_W, sy+OUR_TEX_TILE_W); tnsVertex2d(sx,sy+OUR_TEX_TILE_W);
            tnsColor4dv(laAccentColor(LA_BT_NORMAL));
            tnsPackAs(GL_TRIANGLE_FAN);
            tnsVertex2d(sx, sy); tnsVertex2d(sx+OUR_TEX_TILE_W,sy);
            tnsVertex2d(sx+OUR_TEX_TILE_W, sy+OUR_TEX_TILE_W); tnsVertex2d(sx,sy+OUR_TEX_TILE_W);
            tnsColor4dv(laAccentColor(LA_BT_TEXT));
            tnsPackAs(GL_LINE_LOOP);    
        }
    }
    if(any) tnsFlush();

}


void our_CanvasDrawInit(laUiItem* ui){
    ui->Extra=memAcquireHyper(sizeof(OurCanvasDraw));
    laFirstColumn(laAddTabPage(ui, "New Group"));
    OurCanvasDraw* ocd=ui->Extra;
    ocd->Base.ParentUi=ui;
    ocd->Base.HeightCoeff = 10;
    ocd->Base.ZoomX = 1;
    ocd->Base.ZoomY = 1;
    ocd->Base.ImageDrawAlpha = 1;
    ocd->Base.ImageDrawBorder = 1;
    ocd->Base.AdaptiveLineWidth = 1;
    ocd->Base.ClearBackground = 1;
    ocd->ShowTiles=1;

    int work_grp_cnt[3];
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &work_grp_cnt[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &work_grp_cnt[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &work_grp_cnt[2]);
    printf("max global (total) work group counts x:%i y:%i z:%i\n", work_grp_cnt[0], work_grp_cnt[1], work_grp_cnt[2]);

    int work_grp_size[3];
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &work_grp_size[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &work_grp_size[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &work_grp_size[2]);
    printf("max local (in one shader) work group sizes x:%i y:%i z:%i\n", work_grp_size[0], work_grp_size[1], work_grp_size[2]);

    int work_grp_inv;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &work_grp_inv);
    printf("max local work group invocations %i\n", work_grp_inv);
}
void our_CanvasDrawCanvas(laBoxedTheme *bt, OurPaint *unused_c, laUiItem* ui){
    OurCanvasDraw* ocd=ui->Extra; OurPaint* oc=ui->PP.EndInstance; laCanvasExtra*e=&ocd->Base;
    int W, H; W = ui->R - ui->L; H = ui->B - ui->U;
    tnsFlush();

    if (!e->OffScr || e->OffScr->pColor[0]->Height != ui->B - ui->U || e->OffScr->pColor[0]->Width != ui->R - ui->L){
        if (e->OffScr) tnsDelete2DOffscreen(e->OffScr);
        e->OffScr = tnsCreate2DOffscreen(GL_RGBA, W, H, 0, 0);
    }

    //our_CANVAS_TEST(bt,ui);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    tnsDrawToOffscreen(e->OffScr,1,0);
    tnsViewportWithScissor(0, 0, W, H);
    tnsResetViewMatrix();tnsResetModelMatrix();tnsResetProjectionMatrix();
    tnsOrtho(e->PanX - W * e->ZoomX / 2, e->PanX + W * e->ZoomX / 2, e->PanY - e->ZoomY * H / 2, e->PanY + e->ZoomY * H / 2, 100, -100);
    tnsClearColor(LA_COLOR3(Our->BackgroundColor),1); tnsClearAll();
    //if(ocd->ShowTiles){ our_CanvasDrawTiles(); }
    our_CanvasDrawTextures();

    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
}

OurLayer* our_NewLayer(char* name){
    OurLayer* l=memAcquire(sizeof(OurLayer)); strSafeSet(&l->Name,name); lstPushItem(&Our->Layers, l);
    memAssignRef(Our, &Our->CurrentLayer, l);
    return l;
}
void our_RemoveLayer(OurLayer* l){
    strSafeDestroy(&l->Name); lstRemoveItem(&Our->Layers, l);
    if(Our->CurrentLayer==l){ OurLayer* nl=l->Item.pPrev?l->Item.pPrev:l->Item.pNext; memAssignRef(Our, &Our->CurrentLayer, nl); }
    memLeave(l);
}
OurBrush* our_NewBrush(char* name, real Size, real Hardness, real DabsPerSize, real Transparency, real Smudge, real SmudgeResampleLength,
    int PressureSize, int PressureHardness, int PressureTransparency, int PressureSmudge){
    OurBrush* b=memAcquireHyper(sizeof(OurBrush)); strSafeSet(&b->Name,name); lstAppendItem(&Our->Brushes, b);
    b->Size=Size; b->Hardness=Hardness; b->DabsPerSize=DabsPerSize; b->Transparency=Transparency; b->Smudge=Smudge;
    b->PressureHardness=PressureHardness; b->PressureSize=PressureSize; b->PressureTransparency=PressureTransparency; b->PressureSmudge=PressureSmudge;
    b->SmudgeResampleLength = SmudgeResampleLength;
    memAssignRef(Our, &Our->CurrentBrush, b);
    return b;
}
void our_RemoveBrush(OurBrush* b){
    strSafeDestroy(&b->Name); lstRemoveItem(&Our->Brushes, b);
    if(Our->CurrentLayer==b){ OurLayer* nb=b->Item.pPrev?b->Item.pPrev:b->Item.pNext; memAssignRef(Our, &Our->CurrentBrush, nb); }
    memLeave(b);
}

void our_LayerEnsureTiles(OurLayer* ol, real xmin,real xmax, real ymin,real ymax, int *tl, int *tr, int* tu, int* tb){
    int l=(int)(floor(OUR_TEX_TILE_CTR+xmin/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(l,0,OUR_TEX_TILES_PER_ROW-1);
    int r=(int)(floor(OUR_TEX_TILE_CTR+xmax/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(r,0,OUR_TEX_TILES_PER_ROW-1);
    int u=(int)(floor(OUR_TEX_TILE_CTR+ymax/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(u,0,OUR_TEX_TILES_PER_ROW-1);
    int b=(int)(floor(OUR_TEX_TILE_CTR+ymin/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(b,0,OUR_TEX_TILES_PER_ROW-1);
    //printf("%lf\n",OUR_TEX_TILE_CTR+(real)xmin/OUR_TEX_TILE_W_USE+0.5);
    for(int row=b;row<=u;row++){
        if(!ol->TexTiles[row]){ol->TexTiles[row]=memAcquireSimple(sizeof(OurTexTile*)*OUR_TEX_TILES_PER_ROW);}
        for(int col=l;col<=r;col++){
            if(ol->TexTiles[row][col]) continue;
            ol->TexTiles[row][col]=memAcquireSimple(sizeof(OurTexTile));
            ol->TexTiles[row][col]->Texture=tnsCreate2DTexture(GL_RGBA8,OUR_TEX_TILE_W,OUR_TEX_TILE_W,0);
            float initColor[]={0,0,0,0};
            glClearTexImage(ol->TexTiles[row][col]->Texture->GLTexHandle, 0, GL_BGRA, GL_UNSIGNED_BYTE, &initColor);
        }
    }
    *tl=l; *tr=r; *tu=u; *tb=b;
}
void our_UiToCanvas(laCanvasExtra* ex, laEvent*e, real* x, real *y){
    *x = (real)((real)e->x - (real)(ex->ParentUi->R - ex->ParentUi->L) / 2 - ex->ParentUi->L) * ex->ZoomX + ex->PanX;
    *y = (real)((real)(ex->ParentUi->B - ex->ParentUi->U) / 2 - (real)e->y + ex->ParentUi->U) * ex->ZoomY + ex->PanY;
}
real our_PaintGetDabStepDistance(OurBrush* b, real pressure){
    if(!b->PressureSize) return b->Size/b->DabsPerSize;
    real d=b->Size/b->DabsPerSize*pressure; if(d<1e-2) d=1e-2; return d;
}
int our_PaintGetDabs(OurBrush* b, OurLayer* l, real x, real y, real xto, real yto, real last_pressure, real pressure, int *tl, int *tr, int* tu, int* tb){
    Our->NextDab=0;
    real size=b->Size; real dd=our_PaintGetDabStepDistance(b,last_pressure); real len=tnsDistIdv2(x,y,xto,yto); real rem=b->BrushRemainingDist;
    real alllen=len+rem; real uselen=dd,step=0; if(!len)return 0; if(dd>alllen){ b->BrushRemainingDist+=len; return 0; }
    real xmin=FLT_MAX,xmax=FLT_MIN,ymin=FLT_MAX,ymax=FLT_MIN;
    while(1){
        arrEnsureLength(&Our->Dabs,Our->NextDab,&Our->MaxDab,sizeof(OurDab)); OurDab* od=&Our->Dabs[Our->NextDab];
        real r=tnsGetRatiod(0,len,uselen-rem); od->X=tnsInterpolate(x,xto,r); od->Y=tnsInterpolate(y,yto,r); TNS_CLAMP(r,0,1);
#define pfac(psw) (psw?tnsInterpolate(last_pressure,pressure,r):1);
        real sizepfac=pfac(b->PressureSize)
        od->Size = b->Size*sizepfac;       od->Hardness = b->Hardness*pfac(b->PressureHardness);
        od->Smudge = b->Smudge*pfac(b->PressureSmudge); od->Color[3]=b->Transparency*pfac(b->PressureTransparency);
        tnsVectorSet3v(od->Color,Our->CurrentColor);
#undef pfac;
        xmin=TNS_MIN2(xmin, od->X-od->Size); xmax=TNS_MAX2(xmax, od->X+od->Size); 
        ymin=TNS_MIN2(ymin, od->Y-od->Size); ymax=TNS_MAX2(ymax, od->Y+od->Size);
        if(od->Size>1e-1) Our->NextDab++;
        step=our_PaintGetDabStepDistance(b,sizepfac);
        od->ResampleSmudge=0;
        if(b->Smudge>1e-3){ b->SmudgeAccum+=step; if(b->SmudgeAccum>(b->SmudgeResampleLength*od->Size)){ b->SmudgeAccum-=(b->SmudgeResampleLength*od->Size); od->ResampleSmudge=1; } }
        if(step+uselen<alllen)uselen+=step; else break;
    }
    b->BrushRemainingDist=alllen-uselen;
    if(Our->NextDab) { our_LayerEnsureTiles(l,xmin,xmax,ymin,ymax,tl,tr,tu,tb); return 1; }
    return 0;
}
void our_PaintDoSample(int x, int y, int sx, int sy){
    glUniformSubroutinesuiv(GL_COMPUTE_SHADER,1,&Our->RoutineDoSample);
    glUniform2i(Our->uBrushCorner,x-sx,y-sy);
    glUniform2f(Our->uBrushCenter,x-sx,y-sy);
    glDispatchCompute(1,1,1);
    glUniformSubroutinesuiv(GL_COMPUTE_SHADER,1,&Our->RoutineDoDabs);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void our_PaintDoDab(OurDab* d, int tl, int tr, int tu, int tb){
    int corner[2]; corner[0]=floorf(d->X-d->Size); corner[1]=floorf(d->Y-d->Size);
    real MaxX,MaxY; MaxX=ceil(d->X+d->Size); MaxY=ceil(d->Y+d->Size);
    if(corner[0]>tr||MaxX<tl||corner[1]>tb||MaxY<tu) return;
    corner[0]=corner[0]-tl; corner[1]=corner[1]-tu;
    float center[2]; center[0]=d->X-tl; center[1]=d->Y-tu;
    glUniform2iv(Our->uBrushCorner,1,corner);
    glUniform2fv(Our->uBrushCenter,1,center);
    glUniform1f(Our->uBrushSize,d->Size);
    glUniform1f(Our->uBrushHardness,d->Hardness);
    glUniform1f(Our->uBrushSmudge,d->Smudge);
    glUniform4fv(Our->uBrushColor,1,d->Color);
    glDispatchCompute(ceil(d->Size/16), ceil(d->Size/16), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void our_PaintDoDabs(OurLayer* l,int tl, int tr, int tu, int tb, int Start, int End){
    for(int row=tb;row<=tu;row++){
        for(int col=tl;col<=tr;col++){
            OurTexTile* ott=l->TexTiles[row][col];
            glBindImageTexture(0, ott->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
            int sx=((real)col-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM,sy=((real)row-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM;
            for(int i=Start;i<End;i++){
                our_PaintDoDab(&Our->Dabs[i],sx,sx+OUR_TEX_TILE_W,sy,sy+OUR_TEX_TILE_W);
            }
        }
    }
}
STRUCTURE(OurSmudgeSegement){
    laListItem Item;
    int Start,End;
};
void our_PaintDoDabsWithSmudgeSegments(OurLayer* l,int tl, int tr, int tu, int tb){
    laListHandle Segments={0}; int from=0,to=Our->NextDab; if(!Our->NextDab) return;
    OurSmudgeSegement* oss;
    oss=lstAppendPointerSized(&Segments, 0,sizeof(OurSmudgeSegement));
    for(int i=1;i<to;i++){
        if(Our->Dabs[i].ResampleSmudge){ oss->Start=from; oss->End=i; from=i; oss=lstAppendPointerSized(&Segments, 0,sizeof(OurSmudgeSegement)); }
    }
    oss->Start=from; oss->End=to;

    glUseProgram(Our->CanvasProgram);

    if(!Segments.pFirst){ our_PaintDoDabs(l,tl,tr,tu,tb,0,Our->NextDab); return; }

    while(oss=lstPopItem(&Segments)){
        float x=Our->Dabs[oss->Start].X, y=Our->Dabs[oss->Start].Y;
        int col=(int)(floor(OUR_TEX_TILE_CTR+x/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(col,0,OUR_TEX_TILES_PER_ROW-1);
        int row=(int)(floor(OUR_TEX_TILE_CTR+y/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(row,0,OUR_TEX_TILES_PER_ROW-1);
        glBindImageTexture(0, l->TexTiles[row][col]->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
        int sx=((real)col-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM,sy=((real)row-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM;
        glBindImageTexture(1, Our->SmudgeTexture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
        our_PaintDoSample(x,y,sx,sy);

        //printf("from to %d %d %d\n", oss->Start,oss->End,Our->Dabs[oss->Start].ResampleSmudge);

        for(int row=tb;row<=tu;row++){
            for(int col=tl;col<=tr;col++){
                OurTexTile* ott=l->TexTiles[row][col];
                tnsBindTexture(ott->Texture); glBindImageTexture(0, ott->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
                int sx=((real)col-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM,sy=((real)row-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_SEAM;
                for(int i=oss->Start;i<oss->End;i++){
                    our_PaintDoDab(&Our->Dabs[i],sx,sx+OUR_TEX_TILE_W,sy,sy+OUR_TEX_TILE_W);
                }
            }
        }
    }
}

void our_ReadWidgetColor(laCanvasExtra*e,int x,int y){
    uint8_t color[4];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, e->OffScr->FboHandle);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glReadPixels(x,y,1,1, GL_RGBA, GL_UNSIGNED_BYTE, color);
    real a=(real)color[3]/255;
    Our->CurrentColor[0]=(real)color[0]/255*a;
    Our->CurrentColor[1]=(real)color[1]/255*a;
    Our->CurrentColor[2]=(real)color[2]/255*a;
}

int ourinv_NewLayer(laOperator* a, laEvent* e){
    our_NewLayer("Our Layer"); laNotifyUsers("our.canvas.layers");
    return LA_FINISHED;
}
int ourinv_RemoveLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l) return LA_CANCELED;
    our_RemoveLayer(l); laNotifyUsers("our.canvas.layers");
    return LA_FINISHED;
}
int ourinv_MoveLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l) return LA_CANCELED;
    char* direction=strGetArgumentString(a->ExtraInstructionsP,"direction");
    if(strSame(direction,"up")&&l->Item.pPrev){ lstMoveUp(&Our->Layers, l); laNotifyUsers("our.canvas.layers"); }
    elif(l->Item.pNext){ lstMoveDown(&Our->Layers, l); laNotifyUsers("our.canvas.layers"); }
    return LA_FINISHED;
}
int ourinv_NewBrush(laOperator* a, laEvent* e){
    our_NewBrush("Our Brush",15,0.95,9,0.5,0.5,5,0,0,0,0); laNotifyUsers("our.brushes");
    return LA_FINISHED;
}
int ourinv_RemoveBrush(laOperator* a, laEvent* e){
    OurBrush* b=a->This?a->This->EndInstance:0; if(!b) return LA_CANCELED;
    our_RemoveLayer(b); laNotifyUsers("our.brushes");
    return LA_FINISHED;
}
int ourinv_MoveBrush(laOperator* a, laEvent* e){
    OurBrush* b=a->This?a->This->EndInstance:0; if(!b) return LA_CANCELED;
    char* direction=strGetArgumentString(a->ExtraInstructionsP,"direction");
    if(strSame(direction,"up")&&b->Item.pPrev){ lstMoveUp(&Our->Brushes, b); laNotifyUsers("our.brushes"); }
    elif(b->Item.pNext){ lstMoveDown(&Our->Brushes, b); laNotifyUsers("our.brushes"); }
    return LA_FINISHED;
}

int ourinv_Paint(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    real x,y; our_UiToCanvas(&ex->Base,e,&x,&y); ex->CanvasLastX=x;ex->CanvasLastY=y;ex->LastPressure=e->Pressure;
    return LA_RUNNING;
}
int ourmod_Paint(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;

    if(e->Type==LA_L_MOUSE_UP || e->Type==LA_R_MOUSE_DOWN || e->Type==LA_ESCAPE_DOWN){ return LA_FINISHED; }

    if(e->Type==LA_MOUSEMOVE||e->Type==LA_L_MOUSE_DOWN){
        real x,y; our_UiToCanvas(&ex->Base,e,&x,&y);
        int tl,tr,tu,tb;
        if(our_PaintGetDabs(ob,l,ex->CanvasLastX,ex->CanvasLastY,x,y,ex->LastPressure,e->Pressure,&tl,&tr,&tu,&tb)){
            our_PaintDoDabsWithSmudgeSegments(l,tl,tr,tu,tb);
        }
        ex->CanvasLastX=x;ex->CanvasLastY=y;ex->LastPressure=e->Pressure;
        laNotifyUsers("our.canvas");
    }

    return LA_RUNNING;
}
int ourinv_PickColor(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    laUiItem* ui=ex->Base.ParentUi;
    our_ReadWidgetColor(ex, e->x-ui->L, ui->B-e->y); laNotifyUsers("our.current_color");
    return LA_RUNNING;
}
int ourmod_PickColor(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    laUiItem* ui=ex->Base.ParentUi;

    if(e->Type==LA_R_MOUSE_UP || e->Type==LA_L_MOUSE_DOWN || e->Type==LA_ESCAPE_DOWN){ return LA_FINISHED; }

    if(e->Type==LA_MOUSEMOVE||e->Type==LA_R_MOUSE_DOWN){
        our_ReadWidgetColor(ex, e->x-ui->L, ui->B-e->y); laNotifyUsers("our.current_color");
    }

    return LA_RUNNING;
}


void* ourget_FirstLayer(void* unused, void* unused1){
    return Our->Layers.pFirst;
}
void* ourget_FirstBrush(void* unused, void* unused1){
    return Our->Brushes.pFirst;
}
void* ourget_our(void* unused, void* unused1){
    return Our;
}
void ourset_LayerMove(OurLayer* l, int move){
    if(move<0 && l->Item.pPrev){ lstMoveUp(&Our->Layers, l); laNotifyUsers("our.canvas.layers"); }
    elif(move>0 && l->Item.pNext){ lstMoveDown(&Our->Layers, l); laNotifyUsers("our.canvas.layers"); }
}
void ourset_BrushMove(OurBrush* b, int move){
    if(move<0 && b->Item.pPrev){ lstMoveUp(&Our->Brushes, b); laNotifyUsers("our.brushes"); }
    elif(move>0 && b->Item.pNext){ lstMoveDown(&Our->Brushes, b); laNotifyUsers("our.brushes"); }
}
void ourset_BackgroundColor(void* unused, real* arr){
    memcpy(Our->BackgroundColor, arr, sizeof(real)*3);
    laNotifyUsers("our.canvas");
}

#define OUR_ADD_PRESSURE_SWITCH(p)\
    laAddEnumItemAs(p,"NONE","None","Not using pressure",0,0);\
    laAddEnumItemAs(p,"ENABLED","Enabled","Using pressure",1,0);

void ourRegisterEverything(){
    laPropContainer* pc; laKeyMapper* km; laProp* p;

    laCreateOperatorType("OUR_new_layer","New Layer","Create a new layer",0,0,0,ourinv_NewLayer,0,'+',0);
    laCreateOperatorType("OUR_remove_layer","Remove Layer","Remove this layer",0,0,0,ourinv_RemoveLayer,0,L'🗴',0);
    laCreateOperatorType("OUR_move_layer","Move Layer","Remove this layer",0,0,0,ourinv_MoveLayer,0,0,0);
    laCreateOperatorType("OUR_new_brush","New Brush","Create a new brush",0,0,0,ourinv_NewBrush,0,'+',0);
    laCreateOperatorType("OUR_remove_brush","Remove Brush","Remove this brush",0,0,0,ourinv_RemoveBrush,0,L'🗴',0);
    laCreateOperatorType("OUR_move_brush","Move Brush","Remove this brush",0,0,0,ourinv_MoveBrush,0,0,0);
    laCreateOperatorType("OUR_paint","Paint","Paint on a layer",0,0,0,ourinv_Paint,ourmod_Paint,0,LA_EXTRA_TO_PANEL);
    laCreateOperatorType("OUR_pick","Pick color","Pick color on the widget",0,0,0,ourinv_PickColor,ourmod_PickColor,0,LA_EXTRA_TO_PANEL);

    laRegisterUiTemplate("panel_canvas", "Canvas", ourui_CanvasPanel, 0, 0,"Our Paint");
    laRegisterUiTemplate("panel_layers", "Layers", ourui_LayersPanel, 0, 0,0);
    laRegisterUiTemplate("panel_brushes", "Brushes", ourui_BrushesPanel, 0, 0,0);
    laRegisterUiTemplate("panel_color", "Color", ourui_ColorPanel, 0, 0,0);
    
    pc=laDefineRoot();
    laAddSubGroup(pc,"our","Our","OurPaint main","our_paint",0,0,0,-1,ourget_our,0,0,0,0,0,0,0);

    pc=laAddPropertyContainer("our_paint","Our Paint","OurPaint main",0,0,sizeof(OurPaint),0,0,1);
    laAddSubGroup(pc,"canvas","Canvas","OurPaint canvas","our_canvas",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"brushes","Brushes","Brushes","our_brush",0,0,ourui_Brush,offsetof(OurPaint,CurrentBrush),0,0,0,0,0,0,offsetof(OurPaint,Brushes),0);
    laAddSubGroup(pc,"current_brush","Current Brush","Current brush","our_brush",0,0,0,offsetof(OurPaint,CurrentBrush),ourget_FirstBrush,0,laget_ListNext,0,0,0,0,LA_UDF_REFER);
    laAddFloatProperty(pc,"current_color","Current Color","Current color used to paint",0,0,0,1,0,0.05,0.8,0,offsetof(OurPaint,CurrentColor),0,0,4,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"background_color","Background Color","Background color of the canvas",0,0,0,1,0,0.05,0.8,0,offsetof(OurPaint,BackgroundColor),0,0,3,0,0,0,0,ourset_BackgroundColor,0,0,0);
    
    pc=laAddPropertyContainer("our_brush","Our Brush","OurPaint brush",0,0,sizeof(OurBrush),0,0,2);
    laAddStringProperty(pc,"name","Name","Name of the layer",0,0,0,0,1,offsetof(OurBrush,Name),0,0,0,0,LA_AS_IDENTIFIER);
    laAddIntProperty(pc,"__move","Move Slider","Move Slider",LA_WIDGET_HEIGHT_ADJUSTER,0,0,0,0,0,0,0,0,0,ourset_LayerMove,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"size","Size","Base size(radius) of the brush",0,0,"px",1000,0,1,10,0,offsetof(OurBrush,Size),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"transparency","Transparency","Transparency of a dab",0,0,0,1,0,0.05,0.5,0,offsetof(OurBrush,Transparency),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"hardness","Hardness","Hardness of the brush",0,0,0,1,0,0.05,0.95,0,offsetof(OurBrush,Hardness),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"smudge","Smudge","Smudge of the brush",0,0,0,1,0,0.05,0.95,0,offsetof(OurBrush,Smudge),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"dabs_per_size","Dabs Per Size","How many dabs per size of the brush",0,0,0,0,0,0,0,0,offsetof(OurBrush,DabsPerSize),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"smudge_resample_length","Smudge Resample Length","How long of a distance (based on size) should elapse before resampling smudge",0,0,0,0,0,0,0,0,offsetof(OurBrush,SmudgeResampleLength),0,0,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"pressure_size","Pressure Size","Use pen pressure to control size",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureSize),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_transparency","Pressure Transparency","Use pen pressure to control transparency",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureTransparency),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_hardness","Pressure Hardness","Use pen pressure to control hardness",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureHardness),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_smudge","Pressure Smudge","Use pen pressure to control smudging",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureSmudge),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    laAddOperatorProperty(pc,"move","Move","Move brush","OUR_move_brush",0,0);
    laAddOperatorProperty(pc,"remove","Remove","Remove brush","OUR_remove_brush",L'🗴',0);

    pc=laAddPropertyContainer("our_canvas","Our Canvas","OurPaint canvas",0,0,sizeof(OurPaint),0,0,1);
    laAddSubGroup(pc,"layers","Layers","Layers","our_layer",0,0,ourui_Layer,offsetof(OurPaint,CurrentLayer),0,0,0,0,0,0,offsetof(OurPaint,Layers),0);
    laAddSubGroup(pc,"current_layer","Current Layer","Current layer","our_layer",0,0,0,offsetof(OurPaint,CurrentLayer),ourget_FirstLayer,0,laget_ListNext,0,0,0,0,LA_UDF_REFER);

    pc=laAddPropertyContainer("our_layer","Our Layer","OurPaint layer",0,0,sizeof(OurLayer),0,0,1);
    laAddStringProperty(pc,"name","Name","Name of the layer",0,0,0,0,1,offsetof(OurLayer,Name),0,0,0,0,LA_AS_IDENTIFIER);
    laAddIntProperty(pc,"__move","Move Slider","Move Slider",LA_WIDGET_HEIGHT_ADJUSTER,0,0,0,0,0,0,0,0,0,ourset_LayerMove,0,0,0,0,0,0,0,0,0);
    //laAddSubGroup(pc,"Rows","Rows","Rows of tiles","our_rows",0,0,0,0,0,0,0,0,0,0,offsetof(OurLayer,Rows),0);
    laAddOperatorProperty(pc,"move","Move","Move Layer","OUR_move_layer",0,0);
    laAddOperatorProperty(pc,"remove","Remove","Remove layer","OUR_remove_layer",L'🗴',0);

    laCanvasTemplate* ct=laRegisterCanvasTemplate("our_CanvasDraw", "our_canvas", 0, our_CanvasDrawCanvas, la_CanvasDrawOverlay, our_CanvasDrawInit, la_CanvasDestroy);
    pc = laCanvasHasExtraProps(ct,sizeof(OurCanvasDraw),2);
    km = &ct->KeyMapper;
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_DOWN, 0, "direction=out");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_UP, 0, "direction=in");
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, LA_KEY_ALT, LA_L_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_M_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "OUR_paint", LA_KM_SEL_UI_EXTRA, 0, LA_L_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "OUR_pick", LA_KM_SEL_UI_EXTRA, 0, LA_R_MOUSE_DOWN, 0, 0);
}


void ourInit(){
    Our=memAcquire(sizeof(OurPaint));

    ourRegisterEverything();

    char error[1024]; int status;

    Our->SmudgeTexture=tnsCreate2DTexture(GL_RGBA,256,1,0);

    Our->CanvasShader = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* source = OUR_CANVAS_SHADER;
    glShaderSource(Our->CanvasShader, 1, &source, NULL);
    glCompileShader(Our->CanvasShader);
    glGetShaderiv(Our->CanvasShader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE){
        glGetShaderInfoLog(Our->CanvasShader, sizeof(error), 0, error);
        printf("Compute shader error:\n%s", error);
        glDeleteShader(Our->CanvasShader);
        return -1;
    }

    Our->CanvasProgram = glCreateProgram();
    glAttachShader(Our->CanvasProgram, Our->CanvasShader);
    glLinkProgram(Our->CanvasProgram);
    glGetProgramiv(Our->CanvasProgram, GL_LINK_STATUS, &status);
    if (status == GL_FALSE){
        glGetProgramInfoLog(Our->CanvasProgram, sizeof(error), 0, error);
        printf("Shader Linking error:\n%s", error);
        return 0;
    }

    Our->uBrushCorner=glGetUniformLocation(Our->CanvasProgram,"uBrushCorner");
    Our->uBrushCenter=glGetUniformLocation(Our->CanvasProgram,"uBrushCenter");
    Our->uBrushSize=glGetUniformLocation(Our->CanvasProgram,"uBrushSize");
    Our->uBrushHardness=glGetUniformLocation(Our->CanvasProgram,"uBrushHardness");
    Our->uBrushSmudge=glGetUniformLocation(Our->CanvasProgram,"uBrushSmudge");
    Our->uBrushColor=glGetUniformLocation(Our->CanvasProgram,"uBrushColor");

    Our->uBrushRoutineSelection=glGetSubroutineUniformLocation(Our->CanvasProgram, GL_COMPUTE_SHADER, "uBrushRoutineSelection");
    Our->RoutineDoDabs=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoDabs");
    Our->RoutineDoSample=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoSample");

    tnsEnableShaderv(T->immShader);
}


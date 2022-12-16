#include "ourpaint.h"
#include "png.h"
#include "lcms2.h"

OurPaint *Our;
extern LA MAIN;
extern tnsMain* T;

const char OUR_CANVAS_SHADER[]="#version 430\n\
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;\n\
layout(rgba16, binding = 0) uniform image2D img;\n\
layout(rgba16, binding = 1) coherent uniform image2D smudge_buckets;\n\
uniform ivec2 uBrushCorner;\n\
uniform vec2 uBrushCenter;\n\
uniform float uBrushSize;\n\
uniform float uBrushHardness;\n\
uniform float uBrushSmudge;\n\
uniform float uBrushSlender;\n\
uniform float uBrushAngle;\n\
uniform float uBrushRecentness;\n\
uniform vec4 uBrushColor;\n\
uniform vec4 uBackgroundColor;\n\
uniform int uBrushErasing;\n\
float atan2(in float y, in float x){\n\
    bool s = (abs(x) > abs(y)); return mix(3.1415926535/2.0 - atan(x,y), atan(y,x), s);\n\
}\n\
vec2 rotate(vec2 v, float angle) {\n\
  float s = sin(angle); float c = cos(angle);\n\
  return mat2(c,-s,s,c) * v;\n\
}\n\
vec4 mix_over(vec4 colora, vec4 colorb){\n\
    vec4 c; c.a=colora.a+colorb.a*(1-colora.a);\n\
    c.rgb=(colora.rgb+colorb.rgb*(1-colora.a));\n\
    return c;\n\
}\n\
int dab(float d, vec4 color, float size, float hardness, float smudge, vec4 smudge_color, vec4 last_color, out vec4 final){\n\
    vec4 cc=color;\n\
    float fac=1-pow(d/size,1+1/(1-hardness+1e-4));\n\
    cc.a=color.a*fac*(1-smudge); cc.rgb=cc.rgb*cc.a;\n\
    float erasing=float(uBrushErasing);\n\
    cc=cc*(1-erasing);\n\
    vec4 c2=mix(last_color,smudge_color,smudge*fac*color.a);\n\
    c2=mix_over(cc,c2);\n\
    c2=mix(c2,c2*(1-fac*color.a),erasing);\n\
    final=c2;\n\
    return 1;\n\
}\n\
subroutine void BrushRoutines();\n\
subroutine(BrushRoutines) void DoDabs(){\n\
    ivec2 px = ivec2(gl_GlobalInvocationID.xy)+uBrushCorner;\n\
    if(px.x<0||px.y<0||px.x>1024||px.y>1024) return;\n\
    vec2 fpx=vec2(px);\n\
    fpx=uBrushCenter+rotate(fpx-uBrushCenter,uBrushAngle);\n\
    fpx.x=uBrushCenter.x+(fpx.x-uBrushCenter.x)*(1+uBrushSlender);\n\
    float dd=distance(fpx,uBrushCenter); if(dd>uBrushSize) return;\n\
    vec4 dabc=imageLoad(img, px);\n\
    vec4 smudgec=mix(imageLoad(smudge_buckets,ivec2(1,0)),imageLoad(smudge_buckets,ivec2(0,0)),uBrushRecentness);\n\
    vec4 final_color;\n\
    dab(dd,uBrushColor,uBrushSize,uBrushHardness,uBrushSmudge,smudgec,dabc,final_color);\n\
    dabc=final_color;\n\
    imageStore(img, px, dabc);\n\
}\n\
subroutine(BrushRoutines) void DoSample(){\n\
    ivec2 p=ivec2(gl_GlobalInvocationID.xy);\n\
    if(p.y!=0) return;\n\
    vec2 sp=round(vec2(sin(float(p.x)),cos(float(p.x)))*uBrushSize);\n\
    ivec2 px=ivec2(sp)+uBrushCorner; if(px.x<0||px.y<0||px.x>1024||px.y>1024) return;\n\
    vec4 color=imageLoad(img, px);\n\
    imageStore(smudge_buckets,ivec2(p.x+128,0),color);\n\
    memoryBarrier();barrier();\n\
    if(uBrushErasing==0 || p.x!=0) return;\n\
    color=vec4(0,0,0,0); for(int i=0;i<32;i++){ color=color+imageLoad(smudge_buckets, ivec2(i+128,0)); }\n\
    color=color/64+imageLoad(img, uBrushCorner)/2; vec4 oldcolor=imageLoad(smudge_buckets, ivec2(0,0));\n\
    imageStore(smudge_buckets,ivec2(1,0),uBrushErasing==2?color:oldcolor);\n\
    imageStore(smudge_buckets,ivec2(0,0),color);\n\
}\n\
subroutine uniform BrushRoutines uBrushRoutineSelection;\n\
\n\
void main() {\n\
    uBrushRoutineSelection();\n\
}\n\
";

void our_CanvasAlphaMix(uint16_t* target, uint16_t* source){
    real a_1=(real)(65535-source[3])/65535;
    target[3]=source[3]+target[3]*a_1;
    target[0]=source[0]+target[0]*a_1;
    target[1]=source[1]+target[1]*a_1;
    target[2]=source[2]+target[2]*a_1;
}

void our_InitsRGBProfile(int Linear, void** ptr, int* psize, char* copyright, char* manufacturer, char* description){
    cmsCIExyYTRIPLE srgb_primaries_pre_quantized = { {0.639998686, 0.330010138, 1.0}, {0.300003784, 0.600003357, 1.0}, {0.150002046, 0.059997204, 1.0} };
    cmsCIExyY d65_srgb_adobe_specs = {0.3127, 0.3290, 1.0};
    cmsToneCurve*tonecurve; cmsToneCurve*curve[3];
    if(Linear){ tonecurve = cmsBuildGamma (NULL, 1.0f); }
    else{
        cmsFloat64Number srgb_parameters[5] = { 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };
        tonecurve=cmsBuildParametricToneCurve(NULL, 4, srgb_parameters);
    }
    curve[0] = curve[1] = curve[2] = tonecurve;
    cmsHPROFILE profile4 = cmsCreateRGBProfile (&d65_srgb_adobe_specs, &srgb_primaries_pre_quantized, curve);
    cmsMLU *copy = cmsMLUalloc(NULL, 1);
    cmsMLUsetASCII(copy, "en", "US", copyright);
    cmsWriteTag(profile4, cmsSigCopyrightTag, copy);
    cmsMLU* manu = cmsMLUalloc(NULL, 1);
    cmsMLUsetASCII(manu, "en", "US", manufacturer);
    cmsWriteTag(profile4, cmsSigDeviceMfgDescTag, manu);
    cmsMLU *desc = cmsMLUalloc(NULL, 1);
    cmsMLUsetASCII(desc, "en", "US", description);
    cmsWriteTag(profile4, cmsSigProfileDescriptionTag, desc);
    cmsSaveProfileToMem(profile4, 0, psize);
    (*ptr)=calloc(1,*psize); cmsSaveProfileToMem(profile4, *ptr, psize);
    cmsMLUfree(copy); cmsMLUfree(manu); cmsMLUfree(desc); cmsFreeToneCurve(tonecurve); cmsCloseProfile(profile4);
}
void our_InitColorProfiles(){
    char* manu="sRGB chromaticities from A Standard Default Color Space for the Internet - sRGB, http://www.w3.org/Graphics/Color/sRGB; and http://www.color.org/specification/ICC1v43_2010-12.pdf";
    our_InitsRGBProfile(1,&Our->icc_LinearsRGB,&Our->iccsize_LinearsRGB,"Copyright Yiming 2022.",manu,"Yiming's linear sRGB icc profile.");
    our_InitsRGBProfile(0,&Our->icc_sRGB,&Our->iccsize_sRGB,"Copyright Yiming 2022.",manu,"Yiming's sRGB icc profile.");
}

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
    laShowItemFull(uil,c,0,"our.canvas.background_color",LA_WIDGET_FLOAT_COLOR,0,0,0);
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
        laShowItem(uil,c,This,"binding")->Expand=1;
        laShowItemFull(uil,c,This,"move",0,"direction=up;icon=🡱;",0,0)->Flags|=LA_UI_FLAGS_ICON;
        laShowItemFull(uil,c,This,"move",0,"direction=down;icon=🡳;",0,0)->Flags|=LA_UI_FLAGS_ICON;
        laEndRow(uil,b);
    }laEndCondition(uil,b1);
}
void ourui_ToolsPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.6); cl=laLeftColumn(c,0);cr=laRightColumn(c,0);
    laUiItem* b1, *b2;
#define OUR_BR b1=laBeginRow(uil,c,0,0);
#define OUR_ER laEndRow(uil,b1);
#define OUR_PRESSURE(a)\
    b2=laOnConditionThat(uil,c,laNot(laPropExpression(0,"our.tools.current_brush.use_nodes")));\
    laShowItemFull(uil,c,0,"our.tools.current_brush." a,0,"text=P",0,0);\
    laEndCondition(uil,b2);

    laShowItem(uil,c,0,"our.tool")->Flags|=LA_UI_FLAGS_EXPAND;
    laUiItem* bt=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.tool"),laIntExpression(OUR_TOOL_PAINT)));{
        laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.tools.current_brush"));{
            laShowLabel(uil,cl,"Mode:",0,0);laShowItem(uil,cr,0,"our.erasing");
            laShowItem(uil,c,0,"our.tools.current_brush.name");
            laShowItem(uil,cl,0,"our.tools.current_brush.use_nodes");

            laUiItem* b3=laOnConditionThat(uil,c,laPropExpression(0,"our.tools.current_brush.use_nodes"));{
                laShowItemFull(uil,cr,0,"LA_panel_activator",0,"text=Edit;panel_id=panel_brush_nodes",0,0);
            }laEndCondition(uil,b3);

            OUR_BR laShowItem(uil,c,0,"our.tools.current_brush.size")->Expand=1; OUR_PRESSURE("pressure_size") OUR_ER
            OUR_BR laShowItem(uil,c,0,"our.tools.current_brush.transparency")->Expand=1; OUR_PRESSURE("pressure_transparency")  OUR_ER
            OUR_BR laShowItem(uil,c,0,"our.tools.current_brush.hardness")->Expand=1;  OUR_PRESSURE("pressure_hardness") OUR_ER
            laShowItem(uil,c,0,"our.tools.current_brush.slender");
            laShowItem(uil,c,0,"our.tools.current_brush.angle");
            OUR_BR laShowItem(uil,c,0,"our.tools.current_brush.smudge")->Expand=1;  OUR_PRESSURE("pressure_smudge")  OUR_ER
            laShowItem(uil,c,0,"our.tools.current_brush.dabs_per_size");
            laShowItem(uil,c,0,"our.tools.current_brush.smudge_resample_length");
            laShowItem(uil,c,0,"our.tools.current_brush.smoothness");
            laShowItem(uil,c,0,"our.tools.current_brush.default_as_eraser");
        }laEndCondition(uil,b);

        laShowSeparator(uil,c);
        laShowLabel(uil,c,"Display:",0,0);
        laShowItem(uil,c,0,"our.enable_brush_circle");
    }laEndCondition(uil,bt);

    bt=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.tool"),laIntExpression(OUR_TOOL_CROP)));{
        laShowItemFull(uil,c,0,"our.show_border",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0);
        laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.show_border"));{
            laShowLabel(uil,cl,"Position:",0,0); laShowItem(uil,cr,0,"our.canvas.position")->Flags|=LA_UI_FLAGS_TRANSPOSE;
            laShowSeparator(uil,c);
            laShowLabel(uil,cl,"Size:",0,0); laShowItem(uil,cr,0,"our.canvas.size")->Flags|=LA_UI_FLAGS_TRANSPOSE;
            laShowSeparator(uil,c);
            laShowItem(uil,c,0,"our.border_alpha");
        }laEndCondition(uil,b);
    }laEndCondition(uil,bt);
}
void ourui_BrushesPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laUiItem* b1, *b2;
    
    laUiItem* bt=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.tool"),laIntExpression(OUR_TOOL_PAINT)));{
        laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.tools.current_brush"));{
            laShowItemFull(uil,c,0,"our.tools.current_brush.size",0,0,0,0);
            laShowItemFull(uil,c,0,"our.tools.current_brush.size_100",0,0,0,0);
            laShowItemFull(uil,c,0,"our.tools.current_brush.size_10",0,0,0,0);
            OUR_BR laShowSeparator(uil,c)->Expand=1; laShowItemFull(uil,c,0,"our.lock_radius",LA_WIDGET_ENUM_HIGHLIGHT,"text=Lock;",0,0); OUR_ER
        }laEndCondition(uil,b);
        laShowItemFull(uil,c,0,"our.tools.brushes",0,0,0,0);
        laShowItem(uil,c,0,"OUR_new_brush");
    }laElse(uil,bt);{
        laShowLabel(uil,c,"Brush tool not selected",0,0);
    }laEndCondition(uil,bt);
}
void ourui_ColorPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    
    laShowItemFull(uil,c,0,"our.current_color",LA_WIDGET_FLOAT_COLOR_HCY,0,0,0);
}
void ourui_BrushPage(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    
    laShowItemFull(uil,c,0,"our.tools.current_brush",LA_WIDGET_COLLECTION_SELECTOR,0,laui_IdentifierOnly,0);
    laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.tools.current_brush"));{
        laShowItemFull(uil,c,0,"our.tools.current_brush.rack_page",LA_WIDGET_COLLECTION_SINGLE,0,0,0)->Extra->HeightCoeff=-1;
    }laEndCondition(uil,b);
}

void our_CanvasDrawTextures(){
    tnsUseImmShader; tnsEnableShaderv(T->immShader);
    for(OurLayer* l=Our->Layers.pLast;l;l=l->Item.pPrev){
        int any=0;
        for(int row=0;row<OUR_TEX_TILES_PER_ROW;row++){
            if(!l->TexTiles[row]) continue;
            for(int col=0;col<OUR_TEX_TILES_PER_ROW;col++){
                if(!l->TexTiles[row][col] || !l->TexTiles[row][col]->Texture) continue;
                int sx=l->TexTiles[row][col]->l,sy=l->TexTiles[row][col]->b;
                real pad=(real)OUR_TEX_TILE_SEAM/OUR_TEX_TILE_W; int seam=OUR_TEX_TILE_SEAM;
                tnsDraw2DTextureArg(l->TexTiles[row][col]->Texture,sx+seam,sy+OUR_TEX_TILE_W-seam,OUR_TEX_TILE_W-seam*2,-OUR_TEX_TILE_W+seam*2,0,pad,pad,pad,pad);
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
            if(!l->TexTiles[row][col] || !l->TexTiles[row][col]->Texture) continue;
            int sx=l->TexTiles[row][col]->l,sy=l->TexTiles[row][col]->b;
            //tnsVertex2d(sx, sy); tnsVertex2d(sx+OUR_TEX_TILE_W,sy);
            //tnsVertex2d(sx+OUR_TEX_TILE_W, sy+OUR_TEX_TILE_W); tnsVertex2d(sx,sy+OUR_TEX_TILE_W);
            //tnsColor4dv(laAccentColor(LA_BT_NORMAL));
            //tnsPackAs(GL_TRIANGLE_FAN);
            tnsVertex2d(sx, sy); tnsVertex2d(sx+OUR_TEX_TILE_W,sy);
            tnsVertex2d(sx+OUR_TEX_TILE_W, sy+OUR_TEX_TILE_W); tnsVertex2d(sx,sy+OUR_TEX_TILE_W);
            tnsColor4dv(laAccentColor(LA_BT_TEXT));
            tnsPackAs(GL_LINE_LOOP);    
        }
    }
    if(any) tnsFlush();
}
void our_CanvasDrawCropping(OurCanvasDraw* ocd){
    tnsUseImmShader; tnsEnableShaderv(T->immShader); tnsUniformUseTexture(T->immShader,0,0); tnsUseNoTexture();
    tnsColor4d(0,0,0,Our->BorderAlpha);
    tnsVertex2d(-1e6,Our->Y); tnsVertex2d(1e6,Our->Y); tnsVertex2d(-1e6,1e6); tnsVertex2d(1e6,1e6); tnsPackAs(GL_TRIANGLE_FAN);
    tnsVertex2d(-1e6,Our->Y); tnsVertex2d(Our->X,Our->Y); tnsVertex2d(Our->X,Our->Y-Our->H); tnsVertex2d(-1e6,Our->Y-Our->H); tnsPackAs(GL_TRIANGLE_FAN);
    tnsVertex2d(1e6,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y-Our->H); tnsVertex2d(1e6,Our->Y-Our->H); tnsPackAs(GL_TRIANGLE_FAN);
    tnsVertex2d(-1e6,Our->Y-Our->H); tnsVertex2d(1e6,Our->Y-Our->H); tnsVertex2d(-1e6,-1e6); tnsVertex2d(1e6,-1e6); tnsPackAs(GL_TRIANGLE_FAN);

    if(Our->Tool==OUR_TOOL_CROP){
        tnsColor4dv(laAccentColor(LA_BT_TEXT));
        tnsVertex2d(Our->X,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y-Our->H); tnsVertex2d(Our->X,Our->Y-Our->H);
        tnsPackAs(GL_LINE_LOOP);
        glLineWidth(3); tnsFlush(); glLineWidth(1);
    }
}
void our_CanvasDrawBrushCircle(OurCanvasDraw* ocd){
    if(!Our->CurrentBrush) return; real v[96];
    tnsUseImmShader();tnsUseNoTexture();
    tnsMakeCircle2d(v,48,ocd->Base.OnX,ocd->Base.OnY,Our->CurrentBrush->Size/ocd->Base.ZoomX+0.5,0);
    tnsColor4d(1,1,1,0.3); tnsVertexArray2d(v,48); tnsPackAs(GL_LINE_LOOP);
    tnsMakeCircle2d(v,48,ocd->Base.OnX,ocd->Base.OnY,Our->CurrentBrush->Size/ocd->Base.ZoomX-0.5,0);
    tnsColor4d(0,0,0,0.3); tnsVertexArray2d(v,48); tnsPackAs(GL_LINE_LOOP);
    tnsFlush();
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
        e->OffScr = tnsCreate2DOffscreen(GL_RGBA16, W, H, 0, 0);
    }

    //our_CANVAS_TEST(bt,ui);
    glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
    //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE);

    tnsDrawToOffscreen(e->OffScr,1,0);
    tnsViewportWithScissor(0, 0, W, H);
    tnsResetViewMatrix();tnsResetModelMatrix();tnsResetProjectionMatrix();
    tnsOrtho(e->PanX - W * e->ZoomX / 2, e->PanX + W * e->ZoomX / 2, e->PanY - e->ZoomY * H / 2, e->PanY + e->ZoomY * H / 2, 100, -100);
    tnsClearColor(LA_COLOR3(Our->BackgroundColor),1); tnsClearAll();
    if(ocd->ShowTiles){ our_CanvasDrawTiles(); }
    our_CanvasDrawTextures();
    if(Our->ShowBorder){ our_CanvasDrawCropping(ocd); }

    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
}
void our_CanvasDrawOverlay(laUiItem* ui,int h){
    laCanvasExtra *e = ui->Extra; OurCanvasDraw* ocd=e;
    laBoxedTheme *bt = (*ui->Type->Theme);

    tnsUseImmShader();tnsEnableShaderv(T->immShader);
    tnsUniformColorMode(T->immShader, 2);
    tnsDraw2DTextureDirectly(e->OffScr->pColor[0], ui->L, ui->U, ui->R - ui->L, ui->B - ui->U);
    tnsFlush();
    tnsUniformColorMode(T->immShader, 0);
    if(Our->EnableBrushCircle && (!ocd->HideBrushCircle)){ our_CanvasDrawBrushCircle(ocd); }
    
    la_CanvasDefaultOverlay(ui, h);
}

int ourextramod_Canvas(laOperator *a, laEvent *e){
    laUiItem *ui = a->Instance; OurCanvasDraw* ocd=ui->Extra;
    if(Our->EnableBrushCircle && (e->Type&LA_MOUSE_EVENT)){ ocd->Base.OnX=e->x; ocd->Base.OnY=e->y; laRedrawCurrentPanel();  }
    return LA_RUNNING_PASS;
}

OurLayer* our_NewLayer(char* name){
    OurLayer* l=memAcquire(sizeof(OurLayer)); strSafeSet(&l->Name,name); lstPushItem(&Our->Layers, l);
    memAssignRef(Our, &Our->CurrentLayer, l);
    return l;
}
void ourbeforefree_Layer(OurLayer* l){
    for(int row=0;row<OUR_TEX_TILES_PER_ROW;row++){ if(!l->TexTiles[row]) continue;
        for(int col=0;col<OUR_TEX_TILES_PER_ROW;col++){ if(!l->TexTiles[row][col]) continue;
            if(l->TexTiles[row][col]->Texture) tnsDeleteTexture(l->TexTiles[row][col]->Texture); l->TexTiles[row][col]->Texture=0;
            if(l->TexTiles[row][col]->Data) free(l->TexTiles[row][col]->Data); l->TexTiles[row][col]->Data=0;
            if(l->TexTiles[row][col]->FullData) free(l->TexTiles[row][col]->FullData); l->TexTiles[row][col]->FullData=0;
            memFree(l->TexTiles[row][col]);
        }
        memFree(l->TexTiles[row]); l->TexTiles[row]=0;
    }
}
void our_RemoveLayer(OurLayer* l){
    strSafeDestroy(&l->Name); lstRemoveItem(&Our->Layers, l);
    if(Our->CurrentLayer==l){ OurLayer* nl=l->Item.pPrev?l->Item.pPrev:l->Item.pNext; memAssignRef(Our, &Our->CurrentLayer, nl); }
    ourbeforefree_Layer(l);
    memLeave(l);
}
OurBrush* our_NewBrush(char* name, real Size, real Hardness, real DabsPerSize, real Transparency, real Smudge, real SmudgeResampleLength,
    int PressureSize, int PressureHardness, int PressureTransparency, int PressureSmudge){
    OurBrush* b=memAcquireHyper(sizeof(OurBrush)); strSafeSet(&b->Name,name); lstAppendItem(&Our->Brushes, b);
    b->Size=Size; b->Hardness=Hardness; b->DabsPerSize=DabsPerSize; b->Transparency=Transparency; b->Smudge=Smudge;
    b->PressureHardness=PressureHardness; b->PressureSize=PressureSize; b->PressureTransparency=PressureTransparency; b->PressureSmudge=PressureSmudge;
    b->SmudgeResampleLength = SmudgeResampleLength;
    memAssignRef(Our, &Our->CurrentBrush, b);
    b->Rack=memAcquireHyper(sizeof(laRackPage)); b->Rack->RackType=LA_RACK_TYPE_DRIVER;
    b->Binding=-1;
    return b;
}
void our_RemoveBrush(OurBrush* b){
    strSafeDestroy(&b->Name);
    if(Our->CurrentBrush==b){ OurBrush* nb=b->Item.pPrev?b->Item.pPrev:b->Item.pNext; memAssignRef(Our, &Our->CurrentBrush, nb); }
    lstRemoveItem(&Our->Brushes, b);
    memLeave(b->Rack); b->Rack=0;
    memLeave(b);
}

int our_BufferAnythingVisible(uint16_t* buf, int elemcount){
    for(int i=0;i<elemcount;i++){
        uint16_t* rgba=&buf[i*4]; if(rgba[3]) return 1;
    }
    return 0;
}
void our_TileEnsureUndoBuffer(OurTexTile* t, real xmin,real xmax, real ymin,real ymax){
    int _l=floor(xmin),_r=ceil(xmax),_u=ceil(ymax),_b=floor(ymin);
    if(t->CopyBuffer) free(t->CopyBuffer); t->CopyBuffer=0; //shouldn't happen
    if(_l>=t->r || _r<t->l || _b>=t->u || _u<t->b || _l==_r || _u==_b) return;
    t->cl=TNS_MAX2(_l,t->l)-t->l;t->cr=TNS_MIN2(_r,t->r)-t->l;t->cu=TNS_MIN2(_u,t->u)-t->b;t->cb=TNS_MAX2(_b,t->b)-t->b;
    int rows=t->cu-t->cb,cols=t->cr-t->cl;
    int bufsize=cols*4*rows*sizeof(uint16_t);
    t->CopyBuffer=calloc(1,bufsize);
    for(int row=0;row<rows;row++){
        memcpy(&t->CopyBuffer[row*cols*4],&t->FullData[((+row+t->cb)*OUR_TEX_TILE_W+t->cl)*4],sizeof(uint16_t)*4*cols);
    }
    uint16_t* temp=malloc(bufsize);
    tnsBindTexture(t->Texture);
    glGetTextureSubImage(t->Texture->GLTexHandle, 0, t->cl, t->cb, 0, cols,rows,1, GL_RGBA, GL_UNSIGNED_SHORT, bufsize, temp);
    for(int row=0;row<rows;row++){
        memcpy(&t->FullData[((+row+t->cb)*OUR_TEX_TILE_W+t->cl)*4],&temp[row*cols*4],sizeof(uint16_t)*4*cols);
    }
}
void our_TileSwapBuffers(OurTexTile* t, uint16_t* data, int IsRedo, int l, int r, int u, int b){
    int rows=u-b,cols=r-l; int bufsize=rows*cols*sizeof(uint16_t)*4;
    uint16_t* temp=malloc(bufsize);
    memcpy(temp,data,bufsize);
    for(int row=0;row<rows;row++){
        memcpy(&data[row*cols*4],&t->FullData[((+row+b)*OUR_TEX_TILE_W+l)*4],sizeof(uint16_t)*4*cols);
        memcpy(&t->FullData[((+row+b)*OUR_TEX_TILE_W+l)*4],&temp[row*cols*4],sizeof(uint16_t)*4*cols);
    }
    tnsBindTexture(t->Texture);
    glGetError();
    uint16_t* use_data=temp;
    if(IsRedo){ use_data=data; }
    glTexSubImage2D(GL_TEXTURE_2D, 0, l, b, cols,rows,GL_RGBA,GL_UNSIGNED_SHORT,use_data);
    free(temp);
}
void ourundo_Tiles(OurUndo* undo){
    for(OurUndoTile* ut=undo->Tiles.pFirst;ut;ut=ut->Item.pNext){
        our_TileSwapBuffers(undo->Layer->TexTiles[ut->row][ut->col], ut->CopyData, 0, ut->l, ut->r, ut->u, ut->b);
    }
    laNotifyUsers("our.canvas");
}
void ourredo_Tiles(OurUndo* undo){
    for(OurUndoTile* ut=undo->Tiles.pFirst;ut;ut=ut->Item.pNext){
        our_TileSwapBuffers(undo->Layer->TexTiles[ut->row][ut->col], ut->CopyData, 0, ut->l, ut->r, ut->u, ut->b);
    }
    laNotifyUsers("our.canvas");
}
void ourundo_Free(OurUndo* undo,int FromLeft){
    OurUndoTile* ut;
    while(ut=lstPopItem(&undo->Tiles)){ free(ut->CopyData); memFree(ut); }
    memFree(undo);
}
#define OUR_XXYY_TO_COL_ROW_RANGE\
    l=(int)(floor(OUR_TEX_TILE_CTR+(xmin-OUR_TEX_TILE_SEAM)/OUR_TEX_TILE_W_USE+0.5));\
    r=(int)(floor(OUR_TEX_TILE_CTR+(xmax+OUR_TEX_TILE_SEAM)/OUR_TEX_TILE_W_USE+0.5));\
    u=(int)(floor(OUR_TEX_TILE_CTR+(ymax+OUR_TEX_TILE_SEAM)/OUR_TEX_TILE_W_USE+0.5));\
    b=(int)(floor(OUR_TEX_TILE_CTR+(ymin-OUR_TEX_TILE_SEAM)/OUR_TEX_TILE_W_USE+0.5));
void our_RecordUndo(OurLayer* ol, real xmin,real xmax, real ymin,real ymax){
    if(xmax<xmin || ymax<ymin) return;
    int l,r,u,b; OUR_XXYY_TO_COL_ROW_RANGE;
    OurUndo* undo=memAcquire(sizeof(OurUndo)); undo->Layer=ol;
    for(int row=b;row<=u;row++){ if(!ol->TexTiles[row]) continue;// Should not happen.
        for(int col=l;col<=r;col++){ if(!ol->TexTiles[row][col]) continue; OurTexTile*t=ol->TexTiles[row][col];
            our_TileEnsureUndoBuffer(t,xmin,xmax,ymin,ymax);
            if(!t->CopyBuffer) continue;
            OurUndoTile* ut=memAcquire(sizeof(OurUndoTile));
            ut->l=t->cl; ut->r=t->cr; ut->u=t->cu; ut->b=t->cb;
            ut->CopyData=t->CopyBuffer; t->CopyBuffer=0;
            ut->col=col;ut->row=row;
            lstAppendItem(&undo->Tiles,ut);
        }
    }
    if(!undo->Tiles.pFirst){ memFree(undo); return; /*unlikely;*/ }
    laFreeNewerDifferences();
    laRecordCustomDifferences(undo,ourundo_Tiles,ourredo_Tiles,ourundo_Free);
    laPushDifferences("Paint",0);
}
void our_LayerEnsureTiles(OurLayer* ol, real xmin,real xmax, real ymin,real ymax, int Aligned, int *tl, int *tr, int* tu, int* tb){
    int l,r,u,b;
    if(Aligned){
        l=(int)(floor(OUR_TEX_TILE_CTR+(xmin)/OUR_TEX_TILE_W_USE+0.5));
        r=(int)(floor(OUR_TEX_TILE_CTR+(xmax-1)/OUR_TEX_TILE_W_USE+0.5));
        u=(int)(floor(OUR_TEX_TILE_CTR+(ymax-1)/OUR_TEX_TILE_W_USE+0.5));
        b=(int)(floor(OUR_TEX_TILE_CTR+(ymin)/OUR_TEX_TILE_W_USE+0.5));
    }else{
        OUR_XXYY_TO_COL_ROW_RANGE;
    }
    TNS_CLAMP(l,0,OUR_TEX_TILES_PER_ROW-1);
    TNS_CLAMP(r,0,OUR_TEX_TILES_PER_ROW-1);
    TNS_CLAMP(u,0,OUR_TEX_TILES_PER_ROW-1);
    TNS_CLAMP(b,0,OUR_TEX_TILES_PER_ROW-1);
    for(int row=b;row<=u;row++){
        if(!ol->TexTiles[row]){ol->TexTiles[row]=memAcquireSimple(sizeof(OurTexTile*)*OUR_TEX_TILES_PER_ROW);}
        for(int col=l;col<=r;col++){
            if(ol->TexTiles[row][col]) continue;
            ol->TexTiles[row][col]=memAcquireSimple(sizeof(OurTexTile));
            OurTexTile*t=ol->TexTiles[row][col];
            t->Texture=tnsCreate2DTexture(GL_RGBA16,OUR_TEX_TILE_W,OUR_TEX_TILE_W,0);
            int sx=((real)col-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE,sy=((real)row-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE;
            t->l=sx-OUR_TEX_TILE_SEAM,t->b=sy-OUR_TEX_TILE_SEAM; t->r=t->l+OUR_TEX_TILE_W; t->u=t->b+OUR_TEX_TILE_W;
            uint16_t initColor[]={0,0,0,0};
            glClearTexImage(t->Texture->GLTexHandle, 0, GL_RGBA, GL_UNSIGNED_SHORT, 0);
            t->FullData=calloc(OUR_TEX_TILE_W*4,OUR_TEX_TILE_W*sizeof(uint16_t));
        }
    }
    *tl=l; *tr=r; *tu=u; *tb=b;
}
void our_TileTextureToImage(OurTexTile* ot, int SX, int SY, int composite){
    if(!ot->Texture) return;
    int bufsize=sizeof(uint16_t)*OUR_TEX_TILE_W_USE*OUR_TEX_TILE_W_USE*4;
    ot->Data=malloc(bufsize); int seam=OUR_TEX_TILE_SEAM; int width=OUR_TEX_TILE_W_USE;
    tnsBindTexture(ot->Texture); glPixelStorei(GL_PACK_ALIGNMENT, 2);
    glGetTextureSubImage(ot->Texture->GLTexHandle, 0, seam, seam, 0, width, width,1, GL_RGBA, GL_UNSIGNED_SHORT, bufsize, ot->Data);
    if(composite){
        for(int row=0;row<OUR_TEX_TILE_W_USE;row++){
            for(int col=0;col<OUR_TEX_TILE_W_USE;col++){
                our_CanvasAlphaMix(&Our->ImageBuffer[((SY+row)*Our->ImageW+SX+col)*4], &ot->Data[(row*OUR_TEX_TILE_W_USE+col)*4]);
            }
        }
    }else{
        for(int row=0;row<OUR_TEX_TILE_W_USE;row++){
            memcpy(&Our->ImageBuffer[((SY+row)*Our->ImageW+SX)*4],&ot->Data[(row*OUR_TEX_TILE_W_USE)*4],sizeof(uint16_t)*4*OUR_TEX_TILE_W_USE);
        }
    }
    free(ot->Data); ot->Data=0;
}
void our_TileImageToTexture(OurTexTile* ot, int SX, int SY){
    if(!ot->Texture) return;
    int pl=(SX!=0)?OUR_TEX_TILE_SEAM:0, pr=((SX+OUR_TEX_TILE_W_USE)!=Our->ImageW)?OUR_TEX_TILE_SEAM:0;
    int pu=(SY!=0)?OUR_TEX_TILE_SEAM:0, pb=((SY+OUR_TEX_TILE_W_USE)!=Our->ImageH)?OUR_TEX_TILE_SEAM:0;
    int bufsize=sizeof(uint16_t)*(OUR_TEX_TILE_W+pl+pr)*(OUR_TEX_TILE_W+pu+pb)*4;
    ot->Data=malloc(bufsize); int width=OUR_TEX_TILE_W_USE+pl+pr, height=OUR_TEX_TILE_W_USE+pu+pb;
    for(int row=0;row<height;row++){
        memcpy(&ot->Data[((row)*width)*4],&Our->ImageBuffer[((SY+row-pu)*Our->ImageW+SX-pl)*4],sizeof(uint16_t)*4*width);
    }
    if(!our_BufferAnythingVisible(ot->Data, bufsize/sizeof(uint16_t)/4)){ tnsDeleteTexture(ot->Texture); ot->Texture=0; }
    else{
        tnsBindTexture(ot->Texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, OUR_TEX_TILE_SEAM-pl, OUR_TEX_TILE_SEAM-pu, width, height, GL_RGBA, GL_UNSIGNED_SHORT, ot->Data);
    }
    free(ot->Data); ot->Data=0;
}
int our_LayerEnsureImageBuffer(OurLayer* ol, int OnlyCalculate){
    int l=1000,r=-1000,u=-1000,b=1000; int any=0;
    for(int row=0;row<OUR_TEX_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        if(row<b) b=row; if(row>u) u=row;
        for(int col=0;col<OUR_TEX_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue;
            if(col<l) l=col; if(col>r) r=col; any++;
        }
    }
    if(!any) return 0;
    Our->ImageW = OUR_TEX_TILE_W_USE*(r-l+1); Our->ImageH = OUR_TEX_TILE_W_USE*(u-b+1);
    Our->ImageX =((real)l-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE; Our->ImageY=((real)b-OUR_TEX_TILE_CTR-0.5)*OUR_TEX_TILE_W_USE;
    if(!OnlyCalculate){
        if(Our->ImageBuffer) free(Our->ImageBuffer);
        Our->ImageBuffer = calloc(Our->ImageW*4,Our->ImageH*sizeof(uint16_t));
    }
    return 1;
}
int our_CanvasEnsureImageBuffer(){
    int x=INT_MAX,y=INT_MAX,w=-INT_MAX,h=-INT_MAX;
    for(OurLayer* l=Our->Layers.pFirst;l;l=l->Item.pNext){
        our_LayerEnsureImageBuffer(l,1);
        if(Our->ImageX<x) x=Our->ImageX; if(Our->ImageY<y) y=Our->ImageY;
        if(Our->ImageW>w) w=Our->ImageW; if(Our->ImageH>h) h=Our->ImageH;
    }
    if(w<0||h<0) return 0;
    Our->ImageX=x; Our->ImageY=y; Our->ImageW=w; Our->ImageH=h;
    if(Our->ImageBuffer) free(Our->ImageBuffer);
    Our->ImageBuffer = calloc(Our->ImageW*4,Our->ImageH*sizeof(uint16_t));
    return 1;
}
void our_CanvasFillImageBufferBackground(){
    int count=Our->ImageW*Our->ImageH;
    Our->BColorU16[0]=Our->BackgroundColor[0]*65535; Our->BColorU16[1]=Our->BackgroundColor[1]*65535;
    Our->BColorU16[2]=Our->BackgroundColor[2]*65535; Our->BColorU16[3]=65535;
    for(int i=0;i<count;i++){
        uint16_t* p=&Our->ImageBuffer[i*4]; tnsVectorSet4v(p,Our->BColorU16);
    }
}
void our_LayerToImageBuffer(OurLayer* ol, int composite){
    for(int row=0;row<OUR_TEX_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        for(int col=0;col<OUR_TEX_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue;
            int sx=ol->TexTiles[row][col]->l+OUR_TEX_TILE_SEAM,sy=ol->TexTiles[row][col]->b+OUR_TEX_TILE_SEAM;
            our_TileTextureToImage(ol->TexTiles[row][col], sx-Our->ImageX, sy-Our->ImageY, composite);
        }
    }
}
void our_LayerToTexture(OurLayer* ol){
    for(int row=0;row<OUR_TEX_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        for(int col=0;col<OUR_TEX_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue;
            int sx=ol->TexTiles[row][col]->l+OUR_TEX_TILE_SEAM,sy=ol->TexTiles[row][col]->b+OUR_TEX_TILE_SEAM;
            our_TileImageToTexture(ol->TexTiles[row][col], sx-Our->ImageX, sy-Our->ImageY);
        }
    }
}
void our_GetFinalDimension(int UseFrame, int* x, int* y, int* w, int* h){
    if(UseFrame){ *x=Our->X; *y=Our->Y; *w=Our->W; *h=Our->H; }
    else{ *x=Our->ImageX; *y=Our->ImageY; *w=Our->ImageW; *h=Our->ImageH; }
    printf("%d %d %d %d, %d %d %d %d\n",Our->X, Our->Y, Our->W, Our->H,Our->ImageX, Our->ImageY, Our->ImageW, Our->ImageH);
}
uint16_t* our_GetFinalRow(int UseFrame, int row, int x, int y, int w, int h, uint16_t* temp){
    if(!UseFrame) return &Our->ImageBuffer[Our->ImageW*(Our->ImageH-row-1)*4];
    int userow=(h-row-1)-(Our->ImageY-(y-h));
    if(userow<0 || userow>=Our->ImageH){ for(int i=0;i<w;i++){ tnsVectorSet4v(&temp[i*4],Our->BColorU16); }  return temp; }
    int sstart=x>Our->ImageX?x-Our->ImageX:0, tstart=x>Our->ImageX?0:Our->ImageX-x;
    int slen=(x+w>Our->ImageX+Our->ImageW)?(Our->ImageW-sstart):(Our->ImageW-sstart-(Our->ImageX+Our->ImageW-x-w));
    for(int i=0;i<sstart;i++){ tnsVectorSet4v(&temp[i*4],Our->BColorU16); }
    for(int i=sstart+slen;i<w;i++){ tnsVectorSet4v(&temp[i*4],Our->BColorU16); }
    memcpy(&temp[tstart*4],&Our->ImageBuffer[(Our->ImageW*(userow)+sstart)*4],slen*sizeof(uint16_t)*4);
    return temp;
}
static void _our_png_write(png_structp png_ptr, png_bytep data, png_size_t length){
    OurLayerWrite* LayerWrite=png_get_io_ptr(png_ptr);
    arrEnsureLength(&LayerWrite->data,LayerWrite->NextData+length,&LayerWrite->MaxData,sizeof(unsigned char));
    memcpy(&LayerWrite->data[LayerWrite->NextData], data, length);
    LayerWrite->NextData+=length;
}
int our_ImageExportPNG(FILE* fp, int WriteToBuffer, void** buf, int* sizeof_buf, int UseFrame){
    if((!fp)&&(!WriteToBuffer)) return 0;
    if(!Our->ImageBuffer) return 0;

    png_structp png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING,0,0,0);
    png_infop info_ptr = png_create_info_struct(png_ptr);

    OurLayerWrite LayerWrite={0};

    if(WriteToBuffer){
        arrEnsureLength(&LayerWrite.data,0,&LayerWrite.MaxData,sizeof(unsigned char));
        png_set_write_fn(png_ptr,&LayerWrite,_our_png_write,0);
    }else{
        png_init_io(png_ptr, fp);
    }

    int X,Y,W,H; our_GetFinalDimension(UseFrame, &X,&Y,&W,&H);
    
    png_set_IHDR(png_ptr, info_ptr,W,H,16,PNG_COLOR_TYPE_RGBA,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_BASE,PNG_FILTER_TYPE_BASE);
    // Should not set gamma, we should set either srgb chunk or iccp. Gimp bug: https://gitlab.gnome.org/GNOME/gimp/-/issues/5363
    // But we still include a gamma 1.0 for convenience in OurPaint internal layer reading.
    //png_set_gAMA(png_ptr,info_ptr,0.45455);
    //png_set_sRGB(png_ptr,info_ptr,PNG_sRGB_INTENT_PERCEPTUAL);
    //png_set_iCCP(png_ptr,info_ptr,"LA_PROFILE",PNG_COMPRESSION_TYPE_BASE,Our->icc_sRGB,Our->iccsize_sRGB);
    png_set_iCCP(png_ptr,info_ptr,"LA_PROFILE",PNG_COMPRESSION_TYPE_BASE,Our->icc_LinearsRGB,Our->iccsize_LinearsRGB);
    png_set_gAMA(png_ptr,info_ptr,1.0);
    // Don't set alpha mode for internal data so read and write would be the same.

    png_write_info(png_ptr, info_ptr);
    png_set_swap(png_ptr);

    uint16_t* temp_row=calloc(W,sizeof(uint16_t)*4);

    for(int i=0;i<H;i++){
        uint16_t* final=our_GetFinalRow(UseFrame,i,X,Y,W,H,temp_row);
        png_write_row(png_ptr, (png_const_bytep)final);
    }

    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    free(Our->ImageBuffer); Our->ImageBuffer=0;

    if(WriteToBuffer){ *buf=LayerWrite.data; *sizeof_buf=LayerWrite.NextData; }

    return 1;
}
void our_EnsureImageBufferOnRead(OurLayer*l, int W, int H, int UseOffsets, int StartX, int StartY){
    int tw=W/OUR_TEX_TILE_W_USE, th=H/OUR_TEX_TILE_W_USE;
    int w=tw*OUR_TEX_TILE_W_USE, h=th*OUR_TEX_TILE_W_USE;
    if(w<W){ tw+=1; w+=OUR_TEX_TILE_W_USE; } if(h<H){ th+=1; h+=OUR_TEX_TILE_W_USE; }

    int ix=UseOffsets?StartX:(-tw/2*OUR_TEX_TILE_W_USE-OUR_TEX_TILE_W_USE/2);
    int iy=UseOffsets?StartY:(th/2*OUR_TEX_TILE_W_USE+OUR_TEX_TILE_W_USE/2);
    int tl,tr,tu,tb;
    our_LayerEnsureTiles(l,ix,ix+W,iy-H,iy,1,&tl,&tr,&tu,&tb);
    our_LayerEnsureImageBuffer(l, 0);
    Our->LoadX = ix-Our->ImageX; Our->LoadY = Our->ImageY+Our->ImageH-iy;
}
static void _our_png_read(png_struct *ps, png_byte *data, png_size_t length){
    OurLayerRead *LayerRead = (OurLayerRead*)png_get_io_ptr(ps);
    memcpy(data,&LayerRead->data[LayerRead->NextData],length);
    LayerRead->NextData+=length;
}
int our_LayerImportPNG(OurLayer* l, FILE* fp, void* buf, int UseProfile, int UseOffsets, int StartX, int StartY){
    int result=0;
    if((!fp&&!buf) || !l) return 0;

    int srgb_intent = 0;
    png_charp icc_profile_name = NULL;
    png_uint_32 icc_proflen = 0;
    int icc_compression_type = 0;
    cmsHPROFILE input_buffer_profile = NULL;
    cmsHTRANSFORM cmsTransform = NULL;
    cmsToneCurve *cmsToneCurve = NULL;
    cmsUInt32Number input_buffer_format = 0;
#if PNG_LIBPNG_VER < 10500    // 1.5.0beta36, according to libpng CHANGES
    png_charp icc_profile = NULL;
#else
    png_bytep icc_profile = NULL;
#endif

    OurLayerRead LayerRead={0};

    png_structp png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING,0,0,0); if (!png_ptr) { return 0; }
    png_infop info_ptr = png_create_info_struct(png_ptr); if (!info_ptr) { return 0; }
    if (setjmp(png_jmpbuf(png_ptr))) { goto cleanup_png_read; }
    if(buf){
        LayerRead.data=buf; png_set_read_fn(png_ptr, &LayerRead, _our_png_read);
    }else{
        png_init_io(png_ptr, fp);
    }
    png_read_info(png_ptr, info_ptr);
    png_set_swap(png_ptr);

    int UseSRGB=0;

    if(UseProfile){
        if(png_get_iCCP (png_ptr, info_ptr, &icc_profile_name, &icc_compression_type, &icc_profile, &icc_proflen)) {
            input_buffer_profile = cmsOpenProfileFromMem(icc_profile, icc_proflen);
            if(!input_buffer_profile) { goto cleanup_png_read; }
            cmsColorSpaceSignature cs_sig = cmsGetColorSpace(input_buffer_profile);
            if (cs_sig != cmsSigRgbData) { logPrint("    png has grayscale iCCP, OurPaint doesn't supported that yet, will load as sRGB.\n");
                cmsCloseProfile(input_buffer_profile); input_buffer_profile = NULL; }
            else{
                char* desc="UNAMED PROFILE";
                cmsUInt32Number len=cmsGetProfileInfoASCII(input_buffer_profile,cmsInfoDescription,"en","US",0,0);
                if(len){ desc=calloc(1,sizeof(char)*len); cmsGetProfileInfoASCII(input_buffer_profile,cmsInfoDescription,"en","US",desc,len); }
                logPrint("    png has iCCP: %s.\n", desc); if(len){ free(desc); }
            }
        }elif(png_get_sRGB(png_ptr,info_ptr,&srgb_intent)){
            logPrint("    png is sRGB.\n");
            UseSRGB=1;
        }else{
            // should use png_get_cHRM and png_get_gAMA, but for simplicity we just treat them as srgb,
            logPrint("    png doesn't contain iCCP or sRGB flags, assuming sRGB.\n");
            UseSRGB=1;
        }
    }

    if (png_get_interlace_type (png_ptr, info_ptr) != PNG_INTERLACE_NONE){ logPrint("    Interlaced png not supported.\n");
        goto cleanup_png_read;
    }

    png_byte ColorType = png_get_color_type(png_ptr, info_ptr);
    png_byte BitDepth = png_get_bit_depth(png_ptr, info_ptr);
    int HasAlpha = ColorType & PNG_COLOR_MASK_ALPHA;
    if (ColorType == PNG_COLOR_TYPE_PALETTE) { png_set_palette_to_rgb(png_ptr); }
    //if (ColorType == PNG_COLOR_TYPE_GRAY && BitDepth < 8) { png_set_expand_gray_1_2_4_to_8(png_ptr); }
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) { png_set_tRNS_to_alpha(png_ptr); HasAlpha = 1; }
    if (BitDepth<16) { png_set_expand_16(png_ptr); }
    if (!HasAlpha) { png_set_add_alpha(png_ptr, 0xFFFF, PNG_FILLER_AFTER); }
    if (ColorType == PNG_COLOR_TYPE_GRAY || ColorType == PNG_COLOR_TYPE_GRAY_ALPHA) { png_set_gray_to_rgb(png_ptr); }
    png_read_update_info(png_ptr, info_ptr);
    if (png_get_bit_depth(png_ptr, info_ptr)!=16) {
        logPrint("    Can't convert png into 16 bits per channel, aborting.\n"); goto cleanup_png_read;
    }
    if (png_get_color_type(png_ptr, info_ptr) != PNG_COLOR_TYPE_RGB_ALPHA) {
        logPrint("    Can't convert png into RGBA format, aborting.\n"); goto cleanup_png_read;
    }
    if (png_get_channels(png_ptr, info_ptr) != 4) {
        logPrint("    Can't convert png into 4 channel RGBA format, aborting.\n"); goto cleanup_png_read;
    }

#ifdef CMS_USE_BIG_ENDIAN
    input_buffer_format = TYPE_RGBA_16;
#else
    input_buffer_format = TYPE_RGBA_16_SE;
#endif

    if(UseProfile){
        cmsTransform = cmsCreateTransform(
            input_buffer_profile, input_buffer_format,
            Our->icc_LinearsRGB, TYPE_RGBA_16,
            INTENT_PERCEPTUAL,
            0
        );
    }

    int W = png_get_image_width(png_ptr, info_ptr);
    int H = png_get_image_height(png_ptr, info_ptr);

    our_EnsureImageBufferOnRead(l,W,H,UseOffsets,StartX,StartY);

    for(int i=0;i<H;i++){
        png_read_row(png_ptr, &Our->ImageBuffer[((H-i-1+Our->LoadY)*Our->ImageW+Our->LoadX)*4], NULL);
    }

    our_LayerToTexture(l);

    result=1;

cleanup_png_read:

    if(input_buffer_profile) cmsCloseProfile(input_buffer_profile);
    if(png_ptr && info_ptr) png_destroy_read_struct(&png_ptr,&info_ptr,0);
    free(Our->ImageBuffer); Our->ImageBuffer=0;

    return result;
}

void our_UiToCanvas(laCanvasExtra* ex, laEvent*e, real* x, real *y){
    *x = (real)((real)e->x - (real)(ex->ParentUi->R - ex->ParentUi->L) / 2 - ex->ParentUi->L) * ex->ZoomX + ex->PanX;
    *y = (real)((real)(ex->ParentUi->B - ex->ParentUi->U) / 2 - (real)e->y + ex->ParentUi->U) * ex->ZoomY + ex->PanY;
}
void our_PaintResetBrushState(OurBrush* b){
    b->BrushRemainingDist = 0; b->SmudgeAccum=0; b->SmudgeRestart=1;
}
real our_PaintGetDabStepDistance(real Size,real DabsPerSize){
    real d=Size/DabsPerSize; if(d<1e-2) d=1e-2; return d;
}
int our_PaintGetDabs(OurBrush* b, OurLayer* l, real x, real y, real xto, real yto,
    real last_pressure, real last_angle_x, real last_angle_y, real pressure, real angle_x, real angle_y,
    int *tl, int *tr, int* tu, int* tb, real* r_xto, real* r_yto){
    Our->NextDab=0;
    if(!b->EvalDabsPerSize) b->EvalDabsPerSize=b->DabsPerSize;
    real smfac=(1-b->Smoothness/1.1); xto=tnsLinearItp(x,xto,smfac); yto=tnsLinearItp(y,yto,smfac);  *r_xto=xto; *r_yto=yto;
    real size=b->Size; real dd=our_PaintGetDabStepDistance(b->EvalSize, b->EvalDabsPerSize); real len=tnsDistIdv2(x,y,xto,yto); real rem=b->BrushRemainingDist;
    real alllen=len+rem; real uselen=dd,step=0; if(!len)return 0; if(dd>alllen){ b->BrushRemainingDist+=len; return 0; }
    real xmin=FLT_MAX,xmax=-FLT_MAX,ymin=FLT_MAX,ymax=-FLT_MAX;
    b->EvalSize=b->Size; b->EvalHardness=b->Hardness; b->EvalSmudge=b->Smudge; b->EvalSmudgeLength=b->SmudgeResampleLength;
    b->EvalTransparency=b->Transparency; b->EvalDabsPerSize=b->DabsPerSize; b->EvalSlender=b->Slender; b->EvalAngle=b->Angle;
    b->EvalSpeed=tnsDistIdv2(x,y,xto,yto)/b->Size;
    if(Our->ResetBrush){ b->LastX=x; b->LastY=y; b->LastAngle=atan2(yto-y,xto-x); b->EvalStrokeLength=0; Our->ResetBrush=0; }
    real this_angle=atan2(yto-y,xto-x);

    while(1){
        arrEnsureLength(&Our->Dabs,Our->NextDab,&Our->MaxDab,sizeof(OurDab)); OurDab* od=&Our->Dabs[Our->NextDab];
        real r=tnsGetRatiod(0,len,uselen-rem); od->X=tnsInterpolate(x,xto,r); od->Y=tnsInterpolate(y,yto,r); TNS_CLAMP(r,0,1);
        b->LastX=od->X; b->LastY=od->Y; tnsVectorSet3v(b->EvalColor, Our->CurrentColor);
        if(b->UseNodes){
            b->EvalPressure=tnsInterpolate(last_pressure,pressure,r); b->EvalPosition[0]=od->X; b->EvalPosition[1]=od->Y;
            b->EvalOffset[0]=0; b->EvalOffset[1]=0; b->EvalStrokeAngle=tnsInterpolate(b->LastAngle,this_angle,r);
            b->EvalTilt[0]=tnsInterpolate(last_angle_x,angle_x,r); b->EvalTilt[1]=tnsInterpolate(last_angle_y,angle_y,r);
            ourEvalBrush();
            TNS_CLAMP(b->EvalSmudge,0,1); TNS_CLAMP(b->EvalSmudgeLength,0,100000); TNS_CLAMP(b->EvalTransparency,0,1); TNS_CLAMP(b->EvalHardness,0,1);  TNS_CLAMP(b->DabsPerSize,0,100000);
            od->X+=b->EvalOffset[0]; od->Y+=b->EvalOffset[1];
        }
        if(!b->EvalDabsPerSize) b->EvalDabsPerSize=1;
#define pfac(psw) (((!b->UseNodes)&&psw)?tnsInterpolate(last_pressure,pressure,r):1)
        od->Size = b->EvalSize*pfac(b->PressureSize);       od->Hardness = b->EvalHardness*pfac(b->PressureHardness);
        od->Smudge = b->EvalSmudge*pfac(b->PressureSmudge); od->Color[3]=pow(b->EvalTransparency*pfac(b->PressureTransparency),2.718);
        tnsVectorSet3v(od->Color,b->EvalColor);
#undef pfac;
        od->Slender = b->EvalSlender; od->Angle=b->EvalAngle;
        xmin=TNS_MIN2(xmin, od->X-od->Size); xmax=TNS_MAX2(xmax, od->X+od->Size); 
        ymin=TNS_MIN2(ymin, od->Y-od->Size); ymax=TNS_MAX2(ymax, od->Y+od->Size);
        if(od->Size>1e-1) Our->NextDab++;
        step=our_PaintGetDabStepDistance(od->Size, b->EvalDabsPerSize);
        b->EvalStrokeLength+=step/b->Size; b->EvalStrokeLengthAccum+=step/b->Size; if(b->EvalStrokeLengthAccum>1e6){b->EvalStrokeLengthAccum-=1e6;}
        od->ResampleSmudge=0;
        if(b->Smudge>1e-3){ b->SmudgeAccum+=step;
            if(b->SmudgeAccum>(b->EvalSmudgeLength*od->Size)){ b->SmudgeAccum-=(b->EvalSmudgeLength*od->Size); od->ResampleSmudge=1; }
            od->Recentness=b->SmudgeAccum/b->EvalSmudgeLength/od->Size;
        }else{od->Recentness=0;}
        if(step+uselen<alllen)uselen+=step; else break;
    }
    b->LastAngle=this_angle;
    b->BrushRemainingDist=alllen-uselen;
    if(Our->NextDab) {
        our_LayerEnsureTiles(l,xmin,xmax,ymin,ymax,0,tl,tr,tu,tb);
        Our->xmin=TNS_MIN2(Our->xmin,xmin);Our->xmax=TNS_MAX2(Our->xmax,xmax);Our->ymin=TNS_MIN2(Our->ymin,ymin);Our->ymax=TNS_MAX2(Our->ymax,ymax);
        return 1; 
    }
    return 0;
}
void our_PaintDoSample(int x, int y, int sx, int sy, int ssize, int last,int begin_stroke){
    glUniform2i(Our->uBrushCorner,x-sx,y-sy);
    glUniform2f(Our->uBrushCenter,x-sx,y-sy);
    glUniform1f(Our->uBrushSize, ssize);
    glUniform1i(Our->uBrushErasing,last?(begin_stroke?2:1):0);
    glDispatchCompute(1,1,1);
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
    glUniform1f(Our->uBrushSlender,d->Slender);
    glUniform1f(Our->uBrushAngle,d->Angle);
    glUniform1f(Our->uBrushRecentness,d->Recentness);
    glUniform4fv(Our->uBrushColor,1,d->Color);
    glDispatchCompute(ceil(d->Size/16), ceil(d->Size/16), 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void our_PaintDoDabs(OurLayer* l,int tl, int tr, int tu, int tb, int Start, int End){
    for(int row=tb;row<=tu;row++){
        for(int col=tl;col<=tr;col++){
            OurTexTile* ott=l->TexTiles[row][col];
            glBindImageTexture(0, ott->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16);
            int sx=l->TexTiles[row][col]->l,sy=l->TexTiles[row][col]->b;
            for(int i=Start;i<End;i++){
                our_PaintDoDab(&Our->Dabs[i],sx,sx+OUR_TEX_TILE_W,sy,sy+OUR_TEX_TILE_W);
            }
        }
    }
}
STRUCTURE(OurSmudgeSegement){
    laListItem Item;
    int Start,End,Resample;
};
void our_PaintDoDabsWithSmudgeSegments(OurLayer* l,int tl, int tr, int tu, int tb){
    laListHandle Segments={0}; int from=0,to=Our->NextDab; if(!Our->NextDab) return;
    OurSmudgeSegement* oss;
    oss=lstAppendPointerSized(&Segments, 0,sizeof(OurSmudgeSegement));
    for(int i=1;i<to;i++){
        if(Our->Dabs[i].ResampleSmudge){ oss->Start=from; oss->End=i; from=i;
            oss=lstAppendPointerSized(&Segments, 0,sizeof(OurSmudgeSegement));  oss->Resample=1;
        }
    }
    oss->Start=from; oss->End=to;
    if(Our->Dabs[0].ResampleSmudge){ ((OurSmudgeSegement*)Segments.pFirst)->Resample=1; }

    glUseProgram(Our->CanvasProgram);
    glUniform1i(Our->uBrushErasing,Our->Erasing);

    while(oss=lstPopItem(&Segments)){
        if(oss->Resample || Our->CurrentBrush->SmudgeRestart){
            glUniformSubroutinesuiv(GL_COMPUTE_SHADER,1,&Our->RoutineDoSample);
            int x=Our->Dabs[oss->Start].X, y=Our->Dabs[oss->Start].Y; float ssize=Our->Dabs[oss->Start].Size+1.5;
            int colmax=(int)(floor(OUR_TEX_TILE_CTR+(float)(x+ssize)/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(colmax,0,OUR_TEX_TILES_PER_ROW-1);
            int rowmax=(int)(floor(OUR_TEX_TILE_CTR+(float)(y+ssize)/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(rowmax,0,OUR_TEX_TILES_PER_ROW-1);
            int colmin=(int)(floor(OUR_TEX_TILE_CTR+(float)(x-ssize)/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(colmin,0,OUR_TEX_TILES_PER_ROW-1);
            int rowmin=(int)(floor(OUR_TEX_TILE_CTR+(float)(y-ssize)/OUR_TEX_TILE_W_USE+0.5)); TNS_CLAMP(rowmin,0,OUR_TEX_TILES_PER_ROW-1);
            glBindImageTexture(1, Our->SmudgeTexture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16);
            for(int col=colmin;col<=colmax;col++){
                for(int row=rowmin;row<=rowmax;row++){
                    glBindImageTexture(0, l->TexTiles[row][col]->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16);
                    int sx=l->TexTiles[row][col]->l,sy=l->TexTiles[row][col]->b;
                    our_PaintDoSample(x,y,sx,sy,ssize,(col==colmax)&&(row==rowmax),Our->CurrentBrush->SmudgeRestart);
                }
            }
            Our->CurrentBrush->SmudgeRestart=0;
            glUniformSubroutinesuiv(GL_COMPUTE_SHADER,1,&Our->RoutineDoDabs);
            glUniform1i(Our->uBrushErasing,Our->Erasing);
        }

        //printf("from to %d %d %d\n", oss->Start,oss->End,Our->Dabs[oss->Start].ResampleSmudge);

        our_PaintDoDabs(l,tl,tr,tu,tb,oss->Start,oss->End);
    }
}
void our_EnsureEraser(int EventIsEraser){
    if(EventIsEraser==Our->EventErasing){ return; }
    printf("ev e %d %d\n", Our->EventErasing, Our->Erasing);
    int erasing=Our->Erasing; int num=0;
    if(EventIsEraser && (!Our->EventErasing)){ num=TNS_MAX2(Our->EraserID,num);
        for(OurBrush* b=Our->Brushes.pFirst;b;b=b->Item.pNext){
            if(b->Binding==num){ ourset_CurrentBrush(Our,b); laNotifyUsers("our.tools.brushes"); break; }
        }
        Our->Erasing=1; Our->EventErasing=1; laNotifyUsers("our.erasing");
    }
    elif((!EventIsEraser) && Our->EventErasing){ num=TNS_MAX2(Our->PenID,num);
        for(OurBrush* b=Our->Brushes.pFirst;b;b=b->Item.pNext){
            if(b->Binding==num){ ourset_CurrentBrush(Our,b); laNotifyUsers("our.tools.brushes"); break; }
        }
        Our->Erasing=0; Our->EventErasing=0; laNotifyUsers("our.erasing");
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
    //tns2LinearsRGB(Our->CurrentColor);
}

void our_StartCropping(OurCanvasDraw* cd){
    if(cd->CanvasDownX<Our->X){
        if(cd->CanvasDownY<Our->Y-Our->H){ cd->AtCrop=OUR_AT_CROP_BL; }
        elif(cd->CanvasDownY>=Our->Y-Our->H&&cd->CanvasDownY<=Our->Y){ cd->AtCrop=OUR_AT_CROP_L; }
        elif(cd->CanvasDownY>Our->Y){ cd->AtCrop=OUR_AT_CROP_UL; }
    }elif(cd->CanvasDownX>=Our->X&&cd->CanvasDownX<=Our->X+Our->W){
        if(cd->CanvasDownY<Our->Y-Our->H){ cd->AtCrop=OUR_AT_CROP_B; }
        elif(cd->CanvasDownY>=Our->Y-Our->H&&cd->CanvasDownY<=Our->Y){ cd->AtCrop=OUR_AT_CROP_CENTER; }
        elif(cd->CanvasDownY>Our->Y){ cd->AtCrop=OUR_AT_CROP_U; }
    }elif(cd->CanvasDownX>Our->X+Our->W){
        if(cd->CanvasDownY<Our->Y-Our->H){ cd->AtCrop=OUR_AT_CROP_BR; }
        elif(cd->CanvasDownY>=Our->Y-Our->H&&cd->CanvasDownY<=Our->Y){ cd->AtCrop=OUR_AT_CROP_R; }
        elif(cd->CanvasDownY>Our->Y){ cd->AtCrop=OUR_AT_CROP_UR; }
    }
}
void our_DoCropping(OurCanvasDraw* cd, real x, real y){
    int dx=x-cd->CanvasLastX, dy=y-cd->CanvasLastY;
    if(cd->AtCrop==OUR_AT_CROP_B||cd->AtCrop==OUR_AT_CROP_BL||cd->AtCrop==OUR_AT_CROP_BR){ Our->H-=dy; }
    if(cd->AtCrop==OUR_AT_CROP_U||cd->AtCrop==OUR_AT_CROP_UL||cd->AtCrop==OUR_AT_CROP_UR){ Our->Y+=dy; Our->H+=dy; }
    if(cd->AtCrop==OUR_AT_CROP_L||cd->AtCrop==OUR_AT_CROP_BL||cd->AtCrop==OUR_AT_CROP_UL){ Our->X+=dx; Our->W-=dx; }
    if(cd->AtCrop==OUR_AT_CROP_R||cd->AtCrop==OUR_AT_CROP_BR||cd->AtCrop==OUR_AT_CROP_UR){ Our->W+=dx; }
    if(cd->AtCrop==OUR_AT_CROP_CENTER){ Our->Y+=dy; Our->X+=dx; }
    if(Our->W<32) Our->W=32; if(Our->H<32) Our->H=32; 
    cd->CanvasLastX+=dx; cd->CanvasLastY+=dy;
}

int ourinv_NewLayer(laOperator* a, laEvent* e){
    our_NewLayer("Our Layer"); laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    return LA_FINISHED;
}
int ourinv_RemoveLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l) return LA_CANCELED;
    our_RemoveLayer(l); laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    return LA_FINISHED;
}
int ourinv_MoveLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l) return LA_CANCELED;
    char* direction=strGetArgumentString(a->ExtraInstructionsP,"direction");
    if(strSame(direction,"up")&&l->Item.pPrev){ lstMoveUp(&Our->Layers, l); laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst); }
    elif(l->Item.pNext){ lstMoveDown(&Our->Layers, l); laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst); }
    return LA_FINISHED;
}
int ourinv_ExportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    laInvoke(a, "LA_file_dialog", e, 0, "warn_file_exists=true;", 0);
    return LA_RUNNING;
}
int ourmod_ExportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    if (a->ConfirmData){
        if (a->ConfirmData->StrData){
            if(!our_LayerEnsureImageBuffer(ol, 0)) return LA_FINISHED;
            FILE* fp=fopen(a->ConfirmData->StrData,"wb");
            if(!fp) return LA_FINISHED;
            our_LayerToImageBuffer(ol, 0);
            our_ImageExportPNG(fp, 0, 0, 0, 0);
            fclose(fp);
        }
        return LA_FINISHED;
    }
    return LA_RUNNING;
}
int ourinv_ImportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0;
    laInvoke(a, "LA_file_dialog", e, 0, 0, 0);
    return LA_RUNNING;
}
int ourmod_ImportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0;
    if (a->ConfirmData){
        if (a->ConfirmData->StrData){
            FILE* fp=fopen(a->ConfirmData->StrData,"rb");
            if(!fp) return LA_FINISHED;
            if(!ol) ol=our_NewLayer("Imported");
            our_LayerImportPNG(ol, fp, 0, 0, 0, 0, 0);
            fclose(fp);
        }
        return LA_FINISHED;
    }
    return LA_RUNNING;
}
int ourinv_ExportImage(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    laInvoke(a, "LA_file_dialog", e, 0, "warn_file_exists=true;", 0);
    return LA_RUNNING;
}
int ourmod_ExportImage(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    if (a->ConfirmData){
        if (a->ConfirmData->StrData){
            if(!our_CanvasEnsureImageBuffer()) return LA_FINISHED;
            FILE* fp=fopen(a->ConfirmData->StrData,"wb");
            if(!fp) return LA_FINISHED;
            our_CanvasFillImageBufferBackground();
            for(OurLayer* l=Our->Layers.pLast;l;l=l->Item.pPrev){
                our_LayerToImageBuffer(ol, 1);
            }
            our_ImageExportPNG(fp, 0, 0, 0, Our->ShowBorder);
            fclose(fp);
        }
        return LA_FINISHED;
    }
    return LA_RUNNING;
}

int ourinv_NewBrush(laOperator* a, laEvent* e){
    our_NewBrush("Our Brush",15,0.95,9,0.5,0.5,5,0,0,0,0);
    laNotifyUsers("our.tools.current_brush"); laNotifyUsers("our.tools.brushes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Add brush",0);
    return LA_FINISHED;
}
int ourinv_RemoveBrush(laOperator* a, laEvent* e){
    OurBrush* b=a->This?a->This->EndInstance:0; if(!b) return LA_CANCELED;
    our_RemoveBrush(b);
    laNotifyUsers("our.tools.current_brush"); laNotifyUsers("our.tools.brushes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Remove brush",0);
    return LA_FINISHED;
}
int ourinv_MoveBrush(laOperator* a, laEvent* e){
    OurBrush* b=a->This?a->This->EndInstance:0; if(!b) return LA_CANCELED;
    char* direction=strGetArgumentString(a->ExtraInstructionsP,"direction");
    if(strSame(direction,"up")&&b->Item.pPrev){ lstMoveUp(&Our->Brushes, b); }
    elif(b->Item.pNext){ lstMoveDown(&Our->Brushes, b); }
    laNotifyUsers("our.tools.brushes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Move brush",0);
    return LA_FINISHED;
}
void ourset_CurrentBrush(void* unused, OurBrush* b);
int ourinv_BrushQuickSwitch(laOperator* a, laEvent* e){
    char* id=strGetArgumentString(a->ExtraInstructionsP,"binding"); if(!id){ return LA_CANCELED; }
    int num; int ret=sscanf(id,"%d",&num); if(ret>9||ret<0){ return LA_CANCELED; }
    for(OurBrush* b=Our->Brushes.pFirst;b;b=b->Item.pNext){ 
        if(b->Binding==num){
            ourset_CurrentBrush(Our,b); laNotifyUsers("our.tools.brushes"); break;
        } 
    }
    return LA_FINISHED;
}
int ourinv_BrushResize(laOperator* a, laEvent* e){
    OurBrush* b=Our->CurrentBrush; if(!b) return LA_CANCELED;
    char* direction=strGetArgumentString(a->ExtraInstructionsP,"direction");
    if(strSame(direction,"bigger")){ b->Size*=1.1; }else{ b->Size/=1.1; }
    TNS_CLAMP(b->Size,0,1000);
    laNotifyUsers("our.tools.current_brush.size");
    return LA_FINISHED;
}


int ourinv_Action(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    our_PaintResetBrushState(ob);
    real x,y; our_UiToCanvas(&ex->Base,e,&x,&y); ex->CanvasLastX=x;ex->CanvasLastY=y;ex->LastPressure=-1;ex->LastTilt[0]=e->AngleX;ex->LastTilt[1]=e->AngleY;
    ex->CanvasDownX=x; ex->CanvasDownY=y;
    Our->ActiveTool=Our->Tool; Our->CurrentScale = 1.0f/ex->Base.ZoomX;
    Our->xmin=FLT_MAX;Our->xmax=-FLT_MAX;Our->ymin=FLT_MAX;Our->ymax=-FLT_MAX; Our->ResetBrush=1; ex->HideBrushCircle=1;
    if(Our->ActiveTool==OUR_TOOL_CROP){ if(!Our->ShowBorder) return LA_FINISHED; our_StartCropping(ex); }
    our_EnsureEraser(e->IsEraser);
    laHideCursor();
    return LA_RUNNING;
}
int ourmod_Paint(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    if(e->Type==LA_L_MOUSE_UP || e->Type==LA_R_MOUSE_DOWN || e->Type==LA_ESCAPE_DOWN){
        our_RecordUndo(l,Our->xmin,Our->xmax,Our->ymin,Our->ymax); ex->HideBrushCircle=0; laShowCursor();
        return LA_FINISHED;
    }

    if(e->Type==LA_MOUSEMOVE||e->Type==LA_L_MOUSE_DOWN){
        real x,y; our_UiToCanvas(&ex->Base,e,&x,&y);
        int tl,tr,tu,tb; if(ex->LastPressure<0){ ex->LastPressure=e->Pressure; }
        if(our_PaintGetDabs(ob,l,ex->CanvasLastX,ex->CanvasLastY,x,y,
            ex->LastPressure,ex->LastTilt[0],ex->LastTilt[1],e->Pressure,e->AngleX,e->AngleY,&tl,&tr,&tu,&tb,&ex->CanvasLastX,&ex->CanvasLastY)){
            our_PaintDoDabsWithSmudgeSegments(l,tl,tr,tu,tb);
            laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
        }
        ex->LastPressure=e->Pressure;ex->LastTilt[0]=e->AngleX;ex->LastTilt[1]=e->AngleY;
    }

    return LA_RUNNING;
}
int ourmod_Crop(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    if(e->Type==LA_L_MOUSE_UP || e->Type==LA_R_MOUSE_DOWN || e->Type==LA_ESCAPE_DOWN){  ex->HideBrushCircle=0; laShowCursor(); return LA_FINISHED; }

    if(e->Type==LA_MOUSEMOVE||e->Type==LA_L_MOUSE_DOWN){
        real x,y; our_UiToCanvas(&ex->Base,e,&x,&y);
        our_DoCropping(ex,x,y);
        laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    }

    return LA_RUNNING;
}
int ourmod_Action(laOperator* a, laEvent* e){
    OurCanvasDraw *ex = a->This?a->This->EndInstance:0; if(!ex) return LA_CANCELED;
    switch(Our->ActiveTool){
    case OUR_TOOL_PAINT: OurLayer* l=Our->CurrentLayer; OurBrush* ob=Our->CurrentBrush; if(!l||!ob) return LA_CANCELED;
        return ourmod_Paint(a,e);
    case OUR_TOOL_CROP:
        return ourmod_Crop(a,e);
    default: return LA_FINISHED;
    }
    return LA_RUNNING;
}
int ourinv_PickColor(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    laUiItem* ui=ex->Base.ParentUi;  ex->HideBrushCircle=1;
    our_ReadWidgetColor(ex, e->x-ui->L, ui->B-e->y); laNotifyUsers("our.current_color");
    return LA_RUNNING;
}
int ourmod_PickColor(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    laUiItem* ui=ex->Base.ParentUi;

    if(e->Type==LA_R_MOUSE_UP || e->Type==LA_L_MOUSE_UP || e->Type==LA_ESCAPE_DOWN){  ex->HideBrushCircle=0; return LA_FINISHED; }

    if(e->Type==LA_MOUSEMOVE||e->Type==LA_R_MOUSE_DOWN){
        our_ReadWidgetColor(ex, e->x-ui->L, ui->B-e->y); laNotifyUsers("our.current_color");
    }

    return LA_RUNNING;
}


void ourget_CanvasIdentifier(void* unused, char* buf, char** ptr){
    *ptr="Main canvas";
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
void ourget_LayerTileStart(OurLayer* l, int* xy){
    our_LayerEnsureImageBuffer(l, 1); xy[0]=Our->ImageX; xy[1]=Our->ImageY+Our->ImageH;
}
void ourset_LayerTileStart(OurLayer* l, int* xy){
    Our->TempLoadX = xy[0]; Our->TempLoadY = xy[1];
}
void* ourget_LayerImage(OurLayer* l, int* r_size, int* r_is_copy){
    void* buf=0;
    if(!our_LayerEnsureImageBuffer(l, 0)){ *r_is_copy=0; return 0; }
    our_LayerToImageBuffer(l, 0);
    if(our_ImageExportPNG(0,1,&buf,r_size, 0)){ *r_is_copy=1; return buf; }
    *r_is_copy=0; return buf;
}
void ourset_LayerImage(OurLayer* l, void* data, int size){
    if(!data) return;
    our_LayerImportPNG(l, 0, data, 0, 1, Our->TempLoadX, Our->TempLoadY);
}
void ourset_LayerMove(OurLayer* l, int move){
    if(move<0 && l->Item.pPrev){ lstMoveUp(&Our->Layers, l); laNotifyUsers("our.canvas"); }
    elif(move>0 && l->Item.pNext){ lstMoveDown(&Our->Layers, l); laNotifyUsers("our.canvas"); }
}
void ourset_BrushMove(OurBrush* b, int move){
    if(move<0 && b->Item.pPrev){ lstMoveUp(&Our->Brushes, b); laNotifyUsers("our.tools.brushes"); }
    elif(move>0 && b->Item.pNext){ lstMoveDown(&Our->Brushes, b); laNotifyUsers("our.tools.brushes"); }
}
void ourset_BackgroundColor(void* unused, real* arr){
    memcpy(Our->BackgroundColor, arr, sizeof(real)*3); laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_BorderAlpha(void* unused, real a){
    Our->BorderAlpha=a; laNotifyUsers("our.canvas");  laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_Tool(void* unused, int a){
    Our->Tool=a; laNotifyUsers("our.canvas");
}
void ourset_ShowBorder(void* unused, int a){
    Our->ShowBorder=a; laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_CanvasSize(void* unused, int* wh){
    Our->W=wh[0]; Our->H=wh[1]; if(Our->W<32) Our->W=32; if(Our->H<32) Our->H=32; laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_CanvasPosition(void* unused, int* xy){
    Our->X=xy[0]; Our->Y=xy[1]; laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_LayerPosition(OurLayer* l, int* xy){
    l->OffsetX=xy[0]; l->OffsetY=xy[1]; laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourreset_Canvas(OurPaint* op){
    while(op->Layers.pFirst){ our_RemoveLayer(op->Layers.pFirst); }
}
void ourpropagate_Tools(OurPaint* p, laUDF* udf, int force){
    for(OurBrush* b=p->Brushes.pFirst;b;b=b->Item.pNext){
        if(force || !laget_InstanceActiveUDF(b)){ laset_InstanceUDF(b, udf); }
    }
}
void ourset_CurrentBrush(void* unused, OurBrush* b){
    real r; if(Our->LockRadius) r=Our->CurrentBrush?Our->CurrentBrush->Size:15;
    Our->CurrentBrush=b; if(b && Our->LockRadius) b->Size=r;
    if(b->DefaultAsEraser){ Our->Erasing=1; Our->EraserID=b->Binding; }else{ Our->Erasing=0; Our->PenID=b->Binding; }
    laNotifyUsers("our.tools.current_brush"); laDriverRequestRebuild();
}

#define OUR_ADD_PRESSURE_SWITCH(p)\
    laAddEnumItemAs(p,"NONE","None","Not using pressure",0,0);\
    laAddEnumItemAs(p,"ENABLED","Enabled","Using pressure",1,0);

void ourui_MenuButtons(laUiList *uil, laPropPack *pp, laPropPack *actinst, laColumn *extracol, int context){
    laUiList *muil; laColumn *mc,*c = laFirstColumn(uil);
    muil = laMakeMenuPage(uil, c, "File");{
        mc = laFirstColumn(muil);
        laShowLabel(muil, mc, "Our Paint", 0, 0);
        laShowItem(muil, mc, 0, "OUR_export_image");
        laShowSeparator(muil,mc);
        laShowItem(muil, mc, 0, "OUR_export_layer");
        laShowItem(muil, mc, 0, "OUR_import_layer");
        laui_DefaultMenuButtonsFileEntries(muil,pp,actinst,extracol,0);
    }
    muil = laMakeMenuPage(uil, c, "Options"); {
        mc = laFirstColumn(muil); laui_DefaultMenuButtonsOptionEntries(muil,pp,actinst,extracol,0);
    }
}


void ourPreFrame(){
    if(MAIN.Drivers->NeedRebuild){ ourRebuildBrushEval(); }
}

void ourRegisterEverything(){
    laPropContainer* pc; laKeyMapper* km; laProp* p;

    laCreateOperatorType("OUR_new_layer","New Layer","Create a new layer",0,0,0,ourinv_NewLayer,0,'+',0);
    laCreateOperatorType("OUR_remove_layer","Remove Layer","Remove this layer",0,0,0,ourinv_RemoveLayer,0,L'🗴',0);
    laCreateOperatorType("OUR_move_layer","Move Layer","Remove this layer",0,0,0,ourinv_MoveLayer,0,0,0);
    laCreateOperatorType("OUR_export_layer","Export Layer","Export this layer",0,0,0,ourinv_ExportLayer,ourmod_ExportLayer,L'🖫',0);
    laCreateOperatorType("OUR_import_layer","Import Layer","Import a PNG into a layer",0,0,0,ourinv_ImportLayer,ourmod_ImportLayer,L'🗁',0);
    laCreateOperatorType("OUR_new_brush","New Brush","Create a new brush",0,0,0,ourinv_NewBrush,0,'+',0);
    laCreateOperatorType("OUR_remove_brush","Remove Brush","Remove this brush",0,0,0,ourinv_RemoveBrush,0,L'🗴',0);
    laCreateOperatorType("OUR_move_brush","Move Brush","Remove this brush",0,0,0,ourinv_MoveBrush,0,0,0);
    laCreateOperatorType("OUR_brush_quick_switch","Brush Quick Switch","Brush quick switch",0,0,0,ourinv_BrushQuickSwitch,0,0,0);
    laCreateOperatorType("OUR_brush_resize","Brush Resize","Brush resize",0,0,0,ourinv_BrushResize,0,0,0);
    laCreateOperatorType("OUR_action","Action","Doing action on a layer",0,0,0,ourinv_Action,ourmod_Action,0,LA_EXTRA_TO_PANEL);
    laCreateOperatorType("OUR_pick","Pick color","Pick color on the widget",0,0,0,ourinv_PickColor,ourmod_PickColor,0,LA_EXTRA_TO_PANEL);
    laCreateOperatorType("OUR_export_image","Export Image","Export the image",0,0,0,ourinv_ExportImage,ourmod_ExportImage,L'🖼',0);

    laRegisterUiTemplate("panel_canvas", "Canvas", ourui_CanvasPanel, 0, 0,"Our Paint");
    laRegisterUiTemplate("panel_layers", "Layers", ourui_LayersPanel, 0, 0,0);
    laRegisterUiTemplate("panel_tools", "Tools", ourui_ToolsPanel, 0, 0,0);
    laRegisterUiTemplate("panel_brushes", "Brushes", ourui_BrushesPanel, 0, 0,0);
    laRegisterUiTemplate("panel_color", "Color", ourui_ColorPanel, 0, 0,0);
    laRegisterUiTemplate("panel_brush_nodes", "Brush Nodes", ourui_BrushPage, 0, 0,0);
    
    pc=laDefineRoot();
    laAddSubGroup(pc,"our","Our","OurPaint main","our_paint",0,0,0,-1,ourget_our,0,0,0,0,0,0,LA_UDF_SINGLE);

    pc=laAddPropertyContainer("our_paint","Our Paint","OurPaint main",0,0,sizeof(OurPaint),0,0,1);
    laAddSubGroup(pc,"canvas","Canvas","OurPaint canvas","our_canvas",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"tools","Tools","OurPaint tools","our_tools",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddFloatProperty(pc,"current_color","Current Color","Current color used to paint",0,0,0,1,0,0.05,0.8,0,offsetof(OurPaint,CurrentColor),0,0,3,0,0,0,0,0,0,0,LA_PROP_IS_LINEAR_SRGB);
    laAddFloatProperty(pc,"border_alpha","Border Alpha","Alpha of the border region around the canvas",0,0,0,1,0,0.05,0.5,0,offsetof(OurPaint,BorderAlpha),0,0,0,0,0,0,0,ourset_BorderAlpha,0,0,0);
    p=laAddEnumProperty(pc,"tool","Tool","Tool to use on the canvas",0,0,0,0,0,offsetof(OurPaint,Tool),0,ourset_Tool,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"PAINT","Paint","Paint stuff on the canvas",OUR_TOOL_PAINT,L'🖌');
    laAddEnumItemAs(p,"CROP","Cropping","Crop the focused region",OUR_TOOL_CROP,L'🖼');
    p=laAddEnumProperty(pc,"show_border","Show Border","Whether to show border on the canvas",0,0,0,0,0,offsetof(OurPaint,ShowBorder),0,ourset_ShowBorder,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Dont' show border on the canvas",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Show border on the canvas",1,0);
    p=laAddEnumProperty(pc,"lock_radius","Lock Radius","Lock radius when changing brushes",0,0,0,0,0,offsetof(OurPaint,LockRadius),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Dont' lock radius",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Lock radius when changing brushes",1,0);
    p=laAddEnumProperty(pc,"enable_brush_circle","Brush Circle","Enable brush circle when hovering",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,EnableBrushCircle),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Dont' show brush circle",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Show brush circle on hover",1,0);
    p=laAddEnumProperty(pc,"erasing","Erasing","Is in erasing mode",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,Erasing),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","Draw","Is drawing mode",0,0);
    laAddEnumItemAs(p,"TRUE","Erase","Is erasing mode",1,0);

    pc=laAddPropertyContainer("our_tools","Our Tools","OurPaint tools",0,0,sizeof(OurPaint),0,0,1);
    laPropContainerExtraFunctions(pc,0,0,0,ourpropagate_Tools,0);
    laAddSubGroup(pc,"brushes","Brushes","Brushes","our_brush",0,0,ourui_Brush,offsetof(OurPaint,CurrentBrush),0,0,0,ourset_CurrentBrush,0,0,offsetof(OurPaint,Brushes),0);
    laAddSubGroup(pc,"current_brush","Current Brush","Current brush","our_brush",0,0,0,offsetof(OurPaint,CurrentBrush),ourget_FirstBrush,0,laget_ListNext,ourset_CurrentBrush,0,0,0,LA_UDF_REFER);
    
    pc=laAddPropertyContainer("our_brush","Our Brush","OurPaint brush",0,0,sizeof(OurBrush),0,0,2);
    laAddStringProperty(pc,"name","Name","Name of the layer",0,0,0,0,1,offsetof(OurBrush,Name),0,0,0,0,LA_AS_IDENTIFIER);
    laAddIntProperty(pc,"__move","Move Slider","Move Slider",LA_WIDGET_HEIGHT_ADJUSTER,0,0,0,0,0,0,0,0,0,ourset_BrushMove,0,0,0,0,0,0,0,0,0);
    laAddIntProperty(pc,"binding","Binding","Keyboard binding for shortcut access of the brush",0,0,0,9,-1,1,0,0,offsetof(OurBrush,Binding),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"size_10","~10","Base size(radius) of the brush (max at 10)",0,0,"px",10,0,1,10,0,offsetof(OurBrush,Size),0,0,0,0,0,0,0,0,0,0,LA_UDF_IGNORE);
    laAddFloatProperty(pc,"size_100","~100","Base size(radius) of the brush (max at 100)",0,0,"px",100,0,1,10,0,offsetof(OurBrush,Size),0,0,0,0,0,0,0,0,0,0,LA_UDF_IGNORE);
    laAddFloatProperty(pc,"size","Size","Base size(radius) of the brush",0,0,"px",1000,0,1,10,0,offsetof(OurBrush,Size),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"transparency","Transparency","Transparency of a dab",0,0,0,1,0,0.05,0.5,0,offsetof(OurBrush,Transparency),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"hardness","Hardness","Hardness of the brush",0,0,0,1,0,0.05,0.95,0,offsetof(OurBrush,Hardness),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"smudge","Smudge","Smudge of the brush",0,0,0,1,0,0.05,0.95,0,offsetof(OurBrush,Smudge),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"dabs_per_size","Dabs Per Size","How many dabs per size of the brush",0,0,0,0,0,0,0,0,offsetof(OurBrush,DabsPerSize),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"smudge_resample_length","Smudge Resample Length","How long of a distance (based on size) should elapse before resampling smudge",0,0,0,0,0,0,0,0,offsetof(OurBrush,SmudgeResampleLength),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"slender","Slender","Slenderness of the brush",0,0, 0,10,0,0.1,0,0,offsetof(OurBrush,Slender),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"angle","Angle","Angle of the brush",0,0, 0,TNS_PI,-TNS_PI,0.1,0,0,offsetof(OurBrush,Angle),0,0,0,0,0,0,0,0,0,0,LA_RAD_ANGLE);
    laAddFloatProperty(pc,"smoothness","Smoothness","Smoothness of the brush",0,0, 0,1,0,0.05,0,0,offsetof(OurBrush,Smoothness),0,0,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"pressure_size","Pressure Size","Use pen pressure to control size",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureSize),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_transparency","Pressure Transparency","Use pen pressure to control transparency",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureTransparency),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_hardness","Pressure Hardness","Use pen pressure to control hardness",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureHardness),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_smudge","Pressure Smudge","Use pen pressure to control smudging",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureSmudge),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"use_nodes","Use Nodes","Use nodes to control brush dynamics",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,UseNodes),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Not using nodes",0,0);
    laAddEnumItemAs(p,"ENABLED","Enabled","Using nodes",1,0);
    laAddSubGroup(pc,"rack_page","Rack Page","Nodes rack page of this brush","la_rack_page",0,0,laui_RackPage,offsetof(OurBrush,Rack),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    p=laAddEnumProperty(pc,"default_as_eraser","Default as eraser","Use this brush as a eraser by default",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,DefaultAsEraser),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Default as brush",0,0);
    laAddEnumItemAs(p,"ENABLED","Enabled","Default as eraser",1,0);
    
    laAddOperatorProperty(pc,"move","Move","Move brush","OUR_move_brush",0,0);
    laAddOperatorProperty(pc,"remove","Remove","Remove brush","OUR_remove_brush",L'🗴',0);

    pc=laAddPropertyContainer("our_canvas","Our Canvas","OurPaint canvas",0,0,sizeof(OurPaint),0,0,1);
    laPropContainerExtraFunctions(pc,0,ourreset_Canvas,0,0,0);
    Our->CanvasSaverDummyProp=laPropContainerManageable(pc, offsetof(OurPaint,CanvasSaverDummyList));
    laAddStringProperty(pc,"identifier","Identifier","Canvas identifier placeholder",0,0,0,0,0,0,0,ourget_CanvasIdentifier,0,0,0);
    laAddSubGroup(pc,"layers","Layers","Layers","our_layer",0,0,ourui_Layer,offsetof(OurPaint,CurrentLayer),0,0,0,0,0,0,offsetof(OurPaint,Layers),0);
    laAddSubGroup(pc,"current_layer","Current Layer","Current layer","our_layer",0,0,0,offsetof(OurPaint,CurrentLayer),ourget_FirstLayer,0,laget_ListNext,0,0,0,0,LA_UDF_REFER);
    laAddIntProperty(pc,"size","Size","Size of the cropping area",0,"X,Y","px",0,0,0,2400,0,offsetof(OurPaint,W),0,0,2,0,0,0,0,ourset_CanvasSize,0,0,0);
    laAddIntProperty(pc,"position","Position","Position of the cropping area",0,"X,Y","px",0,0,0,2400,0,offsetof(OurPaint,X),0,0,2,0,0,0,0,ourset_CanvasPosition,0,0,0);
    laAddFloatProperty(pc,"background_color","Background Color","Background color of the canvas",0,0,0,1,0,0.05,0.8,0,offsetof(OurPaint,BackgroundColor),0,0,3,0,0,0,0,ourset_BackgroundColor,0,0,LA_PROP_IS_LINEAR_SRGB);
    
    pc=laAddPropertyContainer("our_layer","Our Layer","OurPaint layer",0,0,sizeof(OurLayer),0,0,1);
    laPropContainerExtraFunctions(pc,ourbeforefree_Layer,ourbeforefree_Layer,0,0,0);
    laAddStringProperty(pc,"name","Name","Name of the layer",0,0,0,0,1,offsetof(OurLayer,Name),0,0,0,0,LA_AS_IDENTIFIER);
    laAddIntProperty(pc,"__move","Move Slider","Move Slider",LA_WIDGET_HEIGHT_ADJUSTER,0,0,0,0,0,0,0,0,0,ourset_LayerMove,0,0,0,0,0,0,0,0,0);
    laAddIntProperty(pc,"offset","Offset","Offset of the layer",0,"X,Y","px",0,0,0,0,0,offsetof(OurLayer,OffsetX),0,0,2,0,0,0,0,ourset_LayerPosition,0,0,0);
    laAddIntProperty(pc,"tile_start","Tile Start","Tile starting position for loading",0,0,0,0,0,0,0,0,0,0,0,2,0,0,ourget_LayerTileStart,0,ourset_LayerTileStart,0,0,LA_UDF_ONLY);
    laAddRawProperty(pc,"image","Image","The image data of this tile",0,0,ourget_LayerImage,ourset_LayerImage,LA_UDF_ONLY);
    laAddOperatorProperty(pc,"move","Move","Move Layer","OUR_move_layer",0,0);
    laAddOperatorProperty(pc,"remove","Remove","Remove layer","OUR_remove_layer",L'🗴',0);

    laCanvasTemplate* ct=laRegisterCanvasTemplate("our_CanvasDraw", "our_canvas", ourextramod_Canvas, our_CanvasDrawCanvas, our_CanvasDrawOverlay, our_CanvasDrawInit, la_CanvasDestroy);
    pc = laCanvasHasExtraProps(ct,sizeof(OurCanvasDraw),2);
    km = &ct->KeyMapper;
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_DOWN, 0, "direction=out");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_UP, 0, "direction=in");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_KEY_DOWN, ',', "direction=out");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_KEY_DOWN, '.', "direction=in");
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, LA_KEY_ALT, LA_L_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_M_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "OUR_action", LA_KM_SEL_UI_EXTRA, 0, LA_L_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "OUR_pick", LA_KM_SEL_UI_EXTRA, 0, LA_R_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "OUR_pick", LA_KM_SEL_UI_EXTRA, LA_KEY_CTRL, LA_L_MOUSE_DOWN, 0, 0);

    km=&MAIN.KeyMap; char buf[128];
    for(int i=0;i<=9;i++){
        sprintf(buf,"binding=%d",i); laAssignNewKey(km, 0, "OUR_brush_quick_switch", 0, 0, LA_KEY_DOWN, '0'+i, buf);
    }
    laAssignNewKey(km, 0, "OUR_brush_resize", 0, 0, LA_KEY_DOWN, '[', "direction=smaller");
    laAssignNewKey(km, 0, "OUR_brush_resize", 0, 0, LA_KEY_DOWN, ']', "direction=bigger");

    laSetMenuBarTemplates(ourui_MenuButtons, laui_DefaultMenuExtras, "OurPaint v0.1");

    ourRegisterNodes();

    laSaveProp("our.canvas");
    laSaveProp("our.tools");

    laGetSaverDummy(Our,Our->CanvasSaverDummyProp);

    laSetFrameCallbacks(ourPreFrame,0,0);
}


void ourInit(){
    Our=memAcquire(sizeof(OurPaint));

    ourRegisterEverything();

    our_InitColorProfiles();

    char error[1024]; int status;

    Our->SmudgeTexture=tnsCreate2DTexture(GL_RGBA16,256,1,0);

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
    Our->uBrushRecentness=glGetUniformLocation(Our->CanvasProgram,"uBrushRecentness");
    Our->uBrushColor=glGetUniformLocation(Our->CanvasProgram,"uBrushColor");
    Our->uBrushSlender=glGetUniformLocation(Our->CanvasProgram,"uBrushSlender");
    Our->uBrushAngle=glGetUniformLocation(Our->CanvasProgram,"uBrushAngle");
    Our->uBrushErasing=glGetUniformLocation(Our->CanvasProgram,"uBrushErasing");

    Our->uBrushRoutineSelection=glGetSubroutineUniformLocation(Our->CanvasProgram, GL_COMPUTE_SHADER, "uBrushRoutineSelection");
    Our->RoutineDoDabs=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoDabs");
    Our->RoutineDoSample=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoSample");

    Our->X=-2800/2; Our->W=2800;
    Our->Y=2400/2;  Our->H=2400;
    Our->BorderAlpha=0.6;

    Our->LockRadius=1;

    Our->PenID=-1;
    Our->EraserID=-1;

    tnsEnableShaderv(T->immShader);
}


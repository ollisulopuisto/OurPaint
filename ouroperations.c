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

#include "ourpaint.h"
#include "png.h"
#include "lcms2.h"
#include <threads.h>
#ifdef __linux__
#include <unistd.h>
#include <libgen.h>
#endif

OurPaint *Our;
extern LA MAIN;
extern tnsMain* T;

#ifdef LA_USE_GLES
#define OUR_CANVAS_GL_PIX GL_R32UI
#define OUR_CANVAS_GL_FORMAT GL_RED_INTEGER
#define OUR_CANVAS_DATA_FORMAT GL_UNSIGNED_INT
#define OUR_CANVAS_PIXEL_SIZE (sizeof(uint32_t))
#define OUR_WORKGROUP_SIZE 16
#define OUR_PIX_MAX 255
#else
#define OUR_CANVAS_GL_PIX GL_RGBA16UI
#define OUR_CANVAS_GL_FORMAT GL_RGBA_INTEGER
#define OUR_CANVAS_DATA_FORMAT GL_UNSIGNED_SHORT
#define OUR_CANVAS_PIXEL_SIZE (sizeof(uint16_t)*4)
#define OUR_WORKGROUP_SIZE 32
#define OUR_PIX_MAX 65535
#endif

void our_LayerEnsureTiles(OurLayer* ol, real xmin,real xmax, real ymin,real ymax, int Aligned, int *tl, int *tr, int* tu, int* tb);
void our_LayerEnsureTileDirect(OurLayer* ol, int col, int row);
void our_RecordUndo(OurLayer* ol, real xmin,real xmax, real ymin,real ymax,int Aligned,int Push);

void our_CanvasAlphaMix(OUR_PIX_COMPACT* target, OUR_PIX_COMPACT* source, real alpha){
    real a_1=(real)(OUR_PIX_MAX-source[3]*alpha)/OUR_PIX_MAX;
    int a=(int)(source[3])*alpha+(int)(target[3])*a_1; TNS_CLAMP(a,0,OUR_PIX_MAX);
    int r=(int)(source[0])*alpha+(int)(target[0])*a_1; TNS_CLAMP(r,0,OUR_PIX_MAX);
    int g=(int)(source[1])*alpha+(int)(target[1])*a_1; TNS_CLAMP(g,0,OUR_PIX_MAX);
    int b=(int)(source[2])*alpha+(int)(target[2])*a_1; TNS_CLAMP(b,0,OUR_PIX_MAX);
    target[3]=a; target[0]=r; target[1]=g; target[2]=b;
}
void our_CanvasAdd(OUR_PIX_COMPACT* target, OUR_PIX_COMPACT* source, real alpha){
    int a=((int)source[3]*alpha+(int)target[3]); TNS_CLAMP(a,0,OUR_PIX_MAX);
    int r=((int)source[0]*alpha+(int)target[0]); TNS_CLAMP(r,0,OUR_PIX_MAX);
    int g=((int)source[1]*alpha+(int)target[1]); TNS_CLAMP(g,0,OUR_PIX_MAX);
    int b=((int)source[2]*alpha+(int)target[2]); TNS_CLAMP(b,0,OUR_PIX_MAX);
    target[3]=a; target[0]=r; target[1]=g; target[2]=b;
}

void our_InitRGBProfile(int Linear,cmsCIExyYTRIPLE* primaries_pre_quantized, void** ptr, int* psize, char* copyright, char* manufacturer, char* description){
    cmsCIExyY d65_srgb_adobe_specs = {0.3127, 0.3290, 1.0};
    cmsToneCurve*tonecurve; cmsToneCurve*curve[3];
    if(Linear==1){ tonecurve = cmsBuildGamma (NULL, 1.0f); }
    elif(Linear==2){
        tonecurve=cmsBuildGamma(NULL,2.19921875);
    }else{
        cmsFloat64Number srgb_parameters[5] = { 2.4, 1.0 / 1.055,  0.055 / 1.055, 1.0 / 12.92, 0.04045 };
        tonecurve=cmsBuildParametricToneCurve(NULL, 4, srgb_parameters);
    }
    curve[0] = curve[1] = curve[2] = tonecurve;
    cmsHPROFILE profile4 = cmsCreateRGBProfile (&d65_srgb_adobe_specs, primaries_pre_quantized, curve);
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
void our_cmsErrorLogger(cmsContext ContextID,cmsUInt32Number ErrorCode,const char *Text){
    logPrintNew("[LCMS] %s\n",Text);
}
void our_InitProofLUT(void** lut, cmsHPROFILE cmyk_profile, cmsHPROFILE rgb_profile){
    real data[OUR_PROOF_PIXCOUNT*3];
    real cmyk8[OUR_PROOF_PIXCOUNT*4];
    int prec=OUR_PROOF_PRECISION;
    for(int i=0;i<prec;i++){
        int counti=i*prec*prec;
        for(int j=0;j<prec;j++){
            int countj=j*prec;
            for(int k=0;k<prec;k++){
                real* p=&data[(counti+countj+k)*3];
                p[0]=((real)i)/OUR_PROOF_VAL; p[1]=((real)j)/OUR_PROOF_VAL; p[2]=((real)k)/OUR_PROOF_VAL;
            }
        }
    }

    *lut=malloc(sizeof(char)*3*OUR_PROOF_PIXCOUNT);
    char* table = *lut;
    
    cmsHTRANSFORM htransform=cmsCreateProofingTransform(rgb_profile,TYPE_RGB_DBL,rgb_profile,TYPE_RGB_8,cmyk_profile,
        INTENT_ABSOLUTE_COLORIMETRIC,INTENT_ABSOLUTE_COLORIMETRIC,cmsFLAGS_HIGHRESPRECALC|cmsFLAGS_SOFTPROOFING);
    cmsDoTransform(htransform,data,table,OUR_PROOF_PIXCOUNT);
}
void our_WriteProofingTable(const char* name,void* data){
    char buf[256]; sprintf(buf,"soft_proof_table_%s.lagui.lut",name);
    FILE* fp=fopen(buf,"wb");
    fwrite(data,sizeof(char)*3*OUR_PROOF_PIXCOUNT,1,fp);
    fclose(fp);
}
void our_InitColorProfiles(){
    cmsSetLogErrorHandler(our_cmsErrorLogger);

    cmsCIExyYTRIPLE srgb_primaries_pre_quantized = { {0.639998686, 0.330010138, 1.0}, {0.300003784, 0.600003357, 1.0}, {0.150002046, 0.059997204, 1.0} };
    cmsCIExyYTRIPLE adobe_primaries_prequantized = { {0.639996511, 0.329996864, 1.0}, {0.210005295, 0.710004866, 1.0}, {0.149997606, 0.060003644, 1.0} };
    cmsCIExyYTRIPLE d65_p3_primaries_prequantized = { {0.680,0.320,1.0}, {0.265,0.690,1.0}, {0.150,0.060,1.0} }; /* https://www.russellcottrell.com/photo/matrixCalculator.htm */
    char* manu="sRGB chromaticities from A Standard Default Color Space for the Internet - sRGB, http://www.w3.org/Graphics/Color/sRGB; and http://www.color.org/specification/ICC1v43_2010-12.pdf";
    our_InitRGBProfile(1,&srgb_primaries_pre_quantized,&Our->icc_LinearsRGB,&Our->iccsize_LinearsRGB,"Copyright Yiming 2022.",manu,"Yiming's linear sRGB icc profile.");
    our_InitRGBProfile(0,&srgb_primaries_pre_quantized,&Our->icc_sRGB,&Our->iccsize_sRGB,"Copyright Yiming 2022.",manu,"Yiming's sRGB icc profile.");
    manu="ClayRGB chromaticities as given in Adobe RGB (1998) Color Image Encoding, Version 2005-05, https://www.adobe.com/digitalimag/pdfs/AdobeRGB1998.pdf";
    our_InitRGBProfile(1,&adobe_primaries_prequantized,&Our->icc_LinearClay,&Our->iccsize_LinearClay,"Copyright Yiming 2022.",manu,"Yiming's Linear ClayRGB icc profile.");
    our_InitRGBProfile(2,&adobe_primaries_prequantized,&Our->icc_Clay,&Our->iccsize_Clay,"Copyright Yiming 2022.",manu,"Yiming's ClayRGB icc profile.");
    manu="ClayRGB chromaticities as given in Adobe RGB (1998) Color Image Encoding, Version 2005-05, https://www.adobe.com/digitalimag/pdfs/AdobeRGB1998.pdf";
    our_InitRGBProfile(1,&d65_p3_primaries_prequantized,&Our->icc_LinearD65P3,&Our->iccsize_LinearD65P3,"Copyright Yiming 2022.",manu,"Yiming's Linear D65 P3 icc profile.");
    our_InitRGBProfile(0,&d65_p3_primaries_prequantized,&Our->icc_D65P3,&Our->iccsize_D65P3,"Copyright Yiming 2022.",manu,"Yiming's D65 P3 icc profile.");

#if 0 // Use this to generate soft proof lut

    char path[4096]; getcwd(path,4096); strcat(path,"/SWOP2006_Coated3v2.icc");
    cmsHPROFILE cmyk = cmsOpenProfileFromFile(path,"r");
    cmsHPROFILE srgb = cmsOpenProfileFromMem(Our->icc_sRGB,Our->iccsize_sRGB);
    cmsHPROFILE clay = cmsOpenProfileFromMem(Our->icc_Clay,Our->iccsize_Clay);
    cmsHPROFILE d65p3 = cmsOpenProfileFromMem(Our->icc_D65P3,Our->iccsize_D65P3);
    our_InitProofLUT(&Our->ProofTablesRGB,cmyk,srgb);
    our_InitProofLUT(&Our->ProofTableClay,cmyk,clay);
    our_InitProofLUT(&Our->ProofTableD65,cmyk,d65p3);
    our_WriteProofingTable("sRGB",Our->ProofTablesRGB);
    our_WriteProofingTable("Clay",Our->ProofTableClay);
    our_WriteProofingTable("D65P3",Our->ProofTableD65);
    laSetProofingLut(Our->ProofTablesRGB, 0);
    laSetProofingLut(Our->ProofTableClay, 1);
    laSetProofingLut(Our->ProofTableD65, 2);

#endif //soft proof
}

void ourui_NotesPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laUiItem* ui=laShowItemFull(uil,c,0,"our.canvas.notes",LA_WIDGET_STRING_MULTI,0,0,0);
    laGeneralUiExtraData* ce=ui->Extra; ce->HeightCoeff = -1;
}
void ourui_CanvasPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laShowInvisibleItem(uil,c,0,"our.canvas_notify");
    laUiItem* ui=laShowCanvas(uil,c,0,"our.canvas",0,-1);
    laCanvasExtra* ce=ui->Extra; ce->ZoomX=ce->ZoomY=1.0f/Our->DefaultScale;
}
void ourui_ThumbnailPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laShowInvisibleItem(uil,c,0,"our.canvas_notify");
    laUiItem* ui=laShowCanvas(uil,c,0,"our.canvas",0,-1);
    laCanvasExtra* ce=ui->Extra; ce->ZoomX=ce->ZoomY=1.0f/Our->DefaultScale;
    ce->SelectThrough = 1;
}
void ourui_Layer(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.7); cl=laLeftColumn(c,0);cr=laRightColumn(c,1);
    laUiItem* b=laBeginRow(uil,cl,0,0);
    laShowHeightAdjuster(uil,cl,This,"__move",0);
    laUiItem* b0=laOnConditionThat(uil,cr,laPropExpression(This,"as_sketch"));{
        laShowLabel(uil,cl,"🖉",0,0);
    }laEndCondition(uil,b0);
    laShowItemFull(uil,cl,This,"name",LA_WIDGET_STRING_PLAIN,0,0,0)->Expand=1;
    laShowItemFull(uil,cl,This,"lock",LA_WIDGET_ENUM_CYCLE_ICON,0,0,0)->Flags|=LA_UI_FLAGS_NO_DECAL|LA_UI_FLAGS_NO_CONFIRM;
    laShowItemFull(uil,cl,This,"hide",LA_WIDGET_ENUM_CYCLE_ICON,0,0,0)->Flags|=LA_UI_FLAGS_NO_DECAL|LA_UI_FLAGS_NO_CONFIRM;
    laEndRow(uil,b);
    laUiItem* b1=laOnConditionToggle(uil,cr,0,0,0,0,0);{ strSafeSet(&b1->ExtraInstructions,"text=☰");
        b=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,This,"remove")->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laShowSeparator(uil,c)->Expand=1;
        laShowItem(uil,c,This,"as_sketch")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laShowSeparator(uil,c);
        laShowItemFull(uil,c,This,"move",0,"direction=up;icon=🡱;",0,0)->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laShowItemFull(uil,c,This,"move",0,"direction=down;icon=🡳;",0,0)->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laEndRow(uil,b);
    }laEndCondition(uil,b1);
}
void ourui_LayersPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.5); cl=laLeftColumn(c,0);cr=laRightColumn(c,0);

    laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.canvas.current_layer"));{
        laUiItem* b1=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,0,"our.canvas.current_layer.name")->Expand=1;
        laShowItem(uil,c,0,"OUR_new_layer")->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laEndRow(uil,b1);
        laShowItem(uil,cl,0,"our.canvas.current_layer.transparency");
        laShowItem(uil,cr,0,"our.canvas.current_layer.blend_mode")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    }laElse(uil,b);{
        laShowItem(uil,c,0,"OUR_new_layer")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    }laEndCondition(uil,b);

    laUiItem* lui=laShowItemFull(uil,c,0,"our.canvas.layers",0,0,0,0);
    lui->Flags|=LA_UI_FLAGS_NO_CONFIRM;

    b=laOnConditionThat(uil,c,laPropExpression(0,"our.canvas.current_layer"));{
        laUiItem* b1=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,&lui->PP,"remove")->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laShowItem(uil,c,&lui->PP,"merge")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
        laShowSeparator(uil,c)->Expand=1;
        laShowItem(uil,c,&lui->PP,"duplicate")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
        laEndRow(uil,b1);
    }laEndCondition(uil,b);

    laShowSeparator(uil,c);

    b=laBeginRow(uil,c,0,0);
    lui=laShowItem(uil,c,0,"OUR_cycle_sketch"); lui->Expand=1; lui->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    laShowSeparator(uil,c); laShowItem(uil,c,0,"our.canvas.sketch_mode")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    laEndRow(uil,b);

    laShowSeparator(uil,c);

    b=laBeginRow(uil,c,0,0);
    lui=laShowLabel(uil,c,"Color Space:",0,0);lui->Expand=1;lui->Flags|=LA_TEXT_ALIGN_RIGHT;
    laShowItem(uil,c,0,"our.canvas.color_interpretation")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    laEndRow(uil,b);

    laShowSeparator(uil,c);

    laShowLabel(uil,c,"Background:",0,0);
    laUiItem* b2=laOnConditionThat(uil,c,laPropExpression(0,"our.lock_background"));{
        laShowItemFull(uil,c,0,"our.lock_background",LA_WIDGET_ENUM_CYCLE,0,0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM|LA_UI_FLAGS_EXIT_WHEN_TRIGGERED;
    }laElse(uil,b2);{
        b=laBeginRow(uil,c,1,0);
        laShowLabel(uil,c,"Color:",0,0);
        laShowItemFull(uil,c,0,"our.canvas.background_color",LA_WIDGET_FLOAT_COLOR,0,0,0);
        laEndRow(uil,b);
        b=laBeginRow(uil,c,1,0);
        laShowLabel(uil,c,"Pattern:",0,0);
        laShowItemFull(uil,c,0,"our.canvas.background_type",0,0,0,0)->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
        laEndRow(uil,b);
        b=laBeginRow(uil,c,1,0);
        laShowItemFull(uil,c,0,"our.canvas.background_random",0,0,0,0);
        laShowItemFull(uil,c,0,"our.canvas.background_factor",0,0,0,0);
        laEndRow(uil,b);
    }laEndCondition(uil,b2);
}
void ourui_Brush(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.7); cl=laLeftColumn(c,0);cr=laRightColumn(c,1);
    laUiItem* b=laBeginRow(uil,cl,0,0);
    laShowHeightAdjuster(uil,cl,This,"__move",0);
    laShowItemFull(uil,cl,This,"name",LA_WIDGET_STRING_PLAIN,0,0,0)->Expand=1;
    laEndRow(uil,b);
    laUiItem* b1=laOnConditionToggle(uil,cr,0,0,0,0,0);{ strSafeSet(&b1->ExtraInstructions,"text=☰");
        b=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,This,"remove")->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laShowItem(uil,c,This,"binding")->Flags|=LA_UI_FLAGS_KNOB;
        laUiItem* ui=laShowItem(uil,c,This,"binding");
        ui->Flags|=LA_UI_FLAGS_PLAIN|LA_UI_FLAGS_NO_LABEL|LA_TEXT_ALIGN_LEFT;ui->Expand=1;
        laShowItem(uil,c,This,"show_in_pages")
            ->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_CYCLE|LA_UI_FLAGS_HIGHLIGHT|LA_UI_FLAGS_TRANSPOSE|LA_UI_FLAGS_NO_CONFIRM|LA_UI_FLAGS_ICON;
        laShowItem(uil,c,This,"duplicate")->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laShowItemFull(uil,c,This,"move",0,"direction=up;icon=🡱;",0,0)->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laShowItemFull(uil,c,This,"move",0,"direction=down;icon=🡳;",0,0)->Flags|=LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
        laEndRow(uil,b);
    }laEndCondition(uil,b1);
}
void ourui_ColorItemSimple(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laShowItemFull(uil,c,This,"color",LA_WIDGET_FLOAT_COLOR,0,0,0)->Flags|=LA_UI_FLAGS_NO_EVENT|LA_UI_FLAGS_NO_DECAL;
}
void ourui_Pallette(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laUiItem* ui=laShowItemFull(uil,c,This,"colors",0,0,ourui_ColorItemSimple,0);ui->SymbolID=7;
    ui->Flags|=LA_UI_FLAGS_NO_DECAL;
}
void ourui_BrushSimple(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    laUiItem* b=laBeginRow(uil,c,0,0);
    laShowItemFull(uil,c,This,"name",LA_WIDGET_STRING_PLAIN,0,0,0)->Expand=1;
    laUiItem* b1=laOnConditionThat(uil,c,laGreaterThan(laPropExpression(This,"binding"),laIntExpression(-1)));
    laShowItemFull(uil,c,This,"binding",LA_WIDGET_INT_PLAIN,0,0,0)->Flags|=LA_UI_FLAGS_NO_LABEL|LA_TEXT_MONO;
    laEndCondition(uil,b1);
    laEndRow(uil,b);
}
void ourui_ToolsPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.5); cl=laLeftColumn(c,0);cr=laRightColumn(c,0);
    laUiItem* b1, *b2;
    laUiItem* cb = laShowInvisibleItem(uil,c,0,"our.tools.current_brush");

#define OUR_BR b1=laBeginRow(uil,c,0,0);
#define OUR_ER laEndRow(uil,b1);
#define OUR_PRESSURE(a) \
    b2=laOnConditionThat(uil,c,laNot(laPropExpression(&cb->PP,"use_nodes")));\
    laShowItemFull(uil,c,&cb->PP, a,0,"text=P",0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;\
    laEndCondition(uil,b2);
#define OUR_TWIST(a) \
    b2=laOnConditionThat(uil,c,laNot(laPropExpression(&cb->PP,"use_nodes")));\
    laShowItemFull(uil,c,&cb->PP, a,0,"text=T",0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;\
    laEndCondition(uil,b2);

    laShowItem(uil,c,0,"our.tool")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
    laUiItem* bt=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.tool"),laIntExpression(OUR_TOOL_PAINT)));{
        laUiItem* b=laOnConditionThat(uil,c,laPropExpression(&cb->PP,0));{
            b1=laBeginRow(uil,c,1,0);
            laShowItem(uil,c,0,"our.erasing")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
            laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.erasing"));{
                laShowItem(uil,c,0,"our.brush_mix")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_DISABLED|LA_UI_FLAGS_NO_CONFIRM;
            }laElse(uil,b);{
                laShowItem(uil,c,0,"our.brush_mix")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
            }laEndCondition(uil,b);
            laEndRow(uil,b1);
            laShowLabel(uil,c,"Brush Settings:",0,0);
            laShowItem(uil,c,&cb->PP,"name")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
            laShowItem(uil,cl,&cb->PP,"use_nodes")->Flags|=LA_UI_FLAGS_NO_CONFIRM;

            laUiItem* b3=laOnConditionThat(uil,c,laPropExpression(&cb->PP,"use_nodes"));{
                laShowItemFull(uil,cr,0,"LA_panel_activator",0,"text=Edit;panel_id=panel_brush_nodes",0,0);
            }laEndCondition(uil,b3);

            OUR_BR laShowItem(uil,c,&cb->PP,"size_offset")->Expand=1; OUR_PRESSURE("pressure_size") OUR_ER
            OUR_BR laShowItem(uil,c,&cb->PP,"transparency")->Expand=1; OUR_PRESSURE("pressure_transparency")  OUR_ER
            OUR_BR laShowItem(uil,c,&cb->PP,"hardness")->Expand=1;  OUR_PRESSURE("pressure_hardness") OUR_ER
            laShowItem(uil,c,&cb->PP,"slender");
            OUR_BR laShowItem(uil,c,&cb->PP,"angle")->Expand=1; OUR_TWIST("twist_angle") OUR_ER;
            laShowItem(uil,c,&cb->PP,"dabs_per_size");
            OUR_BR laShowItem(uil,c,&cb->PP,"smudge")->Expand=1;  OUR_PRESSURE("pressure_smudge")  OUR_ER
            laShowItem(uil,c,&cb->PP,"smudge_resample_length");
            laShowItem(uil,c,&cb->PP,"gunkyness");
            OUR_BR laShowItem(uil,c,&cb->PP,"force")->Expand=1; OUR_PRESSURE("pressure_force") OUR_ER
            laShowSeparator(uil,c);
            laShowItem(uil,c,&cb->PP,"smoothness");
            laShowSeparator(uil,c);
            b2=laOnConditionThat(uil,c,laPropExpression(&cb->PP,"use_nodes"));
                laShowItem(uil,cl,&cb->PP,"c1");
                laShowItem(uil,cr,&cb->PP,"c1_name");
                laShowItem(uil,cl,&cb->PP,"c2");
                laShowItem(uil,cr,&cb->PP,"c2_name");
            laEndCondition(uil,b2);
            laShowSeparator(uil,c);
            laShowLabel(uil,c,"Visual Offset:",0,0);
            OUR_BR laShowItem(uil,c,&cb->PP,"visual_offset")->Expand=1;
            b3=laOnConditionThat(uil,c,laNot(laPropExpression(&cb->PP,"offset_follow_pen_tilt")));{
                laShowItem(uil,c,&cb->PP,"visual_offset_angle");
            }laEndCondition(uil,b3);
            laShowItemFull(uil,c,&cb->PP,"offset_follow_pen_tilt",0,"text=🖍",0,0);
            OUR_ER
            laShowSeparator(uil,c);
            laShowItem(uil,c,&cb->PP,"default_as_eraser");
        }laEndCondition(uil,b);
    }laEndCondition(uil,bt);

    bt=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.tool"),laIntExpression(OUR_TOOL_CROP)));{
        laShowItemFull(uil,cl,0,"our.canvas.show_border",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0);
        laUiItem* b=laOnConditionThat(uil,cl,laPropExpression(0,"our.canvas.show_border"));{
            laUiItem* row=laBeginRow(uil,cl,0,0);
            laShowItem(uil,cl,0,"our.canvas.border_alpha")->Expand=1;
            laShowItem(uil,cl,0,"our.canvas.border_fade_width")->Flags|=LA_UI_FLAGS_KNOB;
            laEndRow(uil,row);
            laShowLabel(uil,cl,"Position:",0,0); laShowItem(uil,cl,0,"our.canvas.position")->Flags|=LA_UI_FLAGS_TRANSPOSE;
            laShowLabel(uil,cl,"Size:",0,0); laShowItem(uil,cl,0,"our.canvas.size")->Flags|=LA_UI_FLAGS_TRANSPOSE;
            laUiItem* b2=laOnConditionThat(uil,cr,laPropExpression(0,"our.canvas.ref_mode"));{
                laShowItem(uil,cl,0,"OUR_crop_to_ref")->Flags|=LA_TEXT_ALIGN_CENTER;
                laUiItem* b1=laBeginRow(uil,cl,1,0);
                laShowItemFull(uil,cl,0,"OUR_crop_to_ref",0,"border=inner;text=Inner",0,0)->Flags|=LA_TEXT_ALIGN_RIGHT;
                laShowItemFull(uil,cl,0,"OUR_crop_to_ref",0,"border=outer;text=Outer",0,0);
                laEndRow(uil,b1);
                b1=laOnConditionThat(uil,cl,laEqual(laPropExpression(0,"our.canvas.ref_mode"),laIntExpression(2)));
                laShowItem(uil,cl,0,"our.canvas.ref_cut_half")->Flags|=LA_UI_FLAGS_EXPAND;
                laEndCondition(uil,b1);
            }laEndCondition(uil,b2);
        }laEndCondition(uil,b);
        
        laShowLabel(uil,cr,"Reference:",0,0);
        laShowItemFull(uil,cr,0,"our.canvas.ref_mode",0,0,0,0)->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
        b=laOnConditionThat(uil,cr,laPropExpression(0,"our.canvas.ref_mode"));{
            laShowItem(uil,cr,0,"our.canvas.ref_alpha");
            laShowItem(uil,cr,0,"our.canvas.ref_category")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
            laShowItem(uil,cr,0,"our.canvas.ref_size")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
            laShowItem(uil,cr,0,"our.canvas.ref_orientation")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
            laShowLabel(uil,cr,"Margins:",0,0); laShowItem(uil,cr,0,"our.canvas.ref_margins")->Flags|=LA_UI_FLAGS_TRANSPOSE;
            laShowLabel(uil,cr,"Paddings:",0,0); laShowItem(uil,cr,0,"our.canvas.ref_paddings")->Flags|=LA_UI_FLAGS_TRANSPOSE;
            laShowItem(uil,cr,0,"our.canvas.ref_middle_margin");
        }laEndCondition(uil,b);
    }laEndCondition(uil,bt);

    laShowSeparator(uil,c);
    laShowLabel(uil,c,"Display:",0,0);
    laShowItem(uil,c,0,"our.preferences.enable_brush_circle");
    laUiItem*b =laBeginRow(uil,c,1,0);
    laShowItem(uil,c,0,"our.preferences.show_stripes");
    laShowItem(uil,c,0,"our.preferences.show_grid");
    laEndRow(uil,b);
}
void ourui_BrushesPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laUiItem* b1, *b2;
    
    laUiItem* bt=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.tool"),laIntExpression(OUR_TOOL_PAINT)));{
        OUR_BR laShowItem(uil,c,0,"our.preferences.smoothness")->Expand=1; laShowItem(uil,c,0,"our.preferences.hardness")->Expand=1; OUR_ER
        laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.tools.current_brush"));{
            laUiItem* uib=laShowItemFull(uil,c,0,"our.preferences.brush_number",0,0,0,0); uib->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
            laUiItem* bn=laOnConditionThat(uil,c,laPropExpression(&uib->PP,""));{
                laShowItemFull(uil,c,0,"our.canvas.brush_base_size",0,0,0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;
            }laElse(uil,bn);{
                laShowItemFull(uil,c,0,"our.preferences.brush_size",0,0,0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;
            }laEndCondition(uil,bn);

            laShowSeparator(uil,c);

            OUR_BR laShowItemFull(uil,c,0,"our.brush_page",0,0,0,0)->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
            laShowSeparator(uil,c)->Expand=1;
            laShowItem(uil,c,0,"OUR_new_brush")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
            OUR_ER
        }laEndCondition(uil,b);
        b=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.brush_page"),laIntExpression(OUR_BRUSH_PAGE_LIST)));{
            laShowItemFull(uil,c,0,"our.tools.brushes",0,0,0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;
            laShowItem(uil,c,0,"OUR_new_brush")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
        }laElse(uil,b);{
            laUiItem* bui=laShowItemFull(uil,c,0,"our.tools.brushes",0,0,ourui_BrushSimple,0);
            bui->SymbolID=2; bui->Flags|=LA_UI_FLAGS_NO_CONFIRM;
        }laEndCondition(uil,b);
    }laElse(uil,bt);{
        laShowLabel(uil,c,"Brush tool not selected",0,0);
        laShowItem(uil,c,0,"our.tool")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
    }laEndCondition(uil,bt);
}
void ourui_ColorPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);

    laUiItem* b=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.canvas.color_interpretation"),laIntExpression(OUR_CANVAS_INTERPRETATION_SRGB)));{
        laShowItemFull(uil,c,0,"our.current_color",LA_WIDGET_FLOAT_COLOR_HCY,0,0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    }laEndCondition(uil,b);
    b=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.canvas.color_interpretation"),laIntExpression(OUR_CANVAS_INTERPRETATION_CLAY)));{
        laShowItemFull(uil,c,0,"our.current_color",LA_WIDGET_FLOAT_COLOR_HCY,0,0,0)->Flags|=LA_UI_FLAGS_COLOR_SPACE_CLAY|LA_UI_FLAGS_NO_CONFIRM;
    }laEndCondition(uil,b);
    b=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.canvas.color_interpretation"),laIntExpression(OUR_CANVAS_INTERPRETATION_D65_P3)));{
        laShowItemFull(uil,c,0,"our.current_color",LA_WIDGET_FLOAT_COLOR_HCY,0,0,0)->Flags|=LA_UI_FLAGS_COLOR_SPACE_D65_P3|LA_UI_FLAGS_NO_CONFIRM;
    }laEndCondition(uil,b);
    b=laBeginRow(uil,c,0,0);
    laShowItem(uil,c,0,"our.preferences.spectral_mode")->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    laShowItem(uil,c,0,"our.current_color")->Expand=1;
    laUiItem* b2=laOnConditionToggle(uil,c,0,0,0,0,0);
    laEndRow(uil,b);
    laShowItem(uil,c,0,"our.color_boost")->Expand=1;
    laElse(uil,b2); laEndRow(uil,b2); laEndCondition(uil,b2);
}
void ourui_PallettesPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laUiItem* b,*b1,*b2;
    b=laBeginRow(uil,c,0,0);
    laShowItemFull(uil,c,0,"our.tools.pallettes",LA_WIDGET_COLLECTION_SELECTOR,0,laui_IdentifierOnly,0)->Flags|=LA_UI_COLLECTION_SIMPLE_SELECTOR;
    laUiItem* ui=laShowInvisibleItem(uil,c,0,"our.tools.current_pallette");
    b1=laOnConditionThat(uil,c,laPropExpression(&ui->PP,""));{
        laUiItem* name=laShowItem(uil,c,&ui->PP,"name");name->Flags|=LA_UI_FLAGS_NO_DECAL; name->Expand=1;
        laShowItem(uil,c,0,"OUR_new_pallette")->Flags|=LA_UI_FLAGS_ICON;
        laEndRow(uil,b);
        laShowItemFull(uil,c,0,"our.tools.current_pallette",LA_WIDGET_COLLECTION_SINGLE,0,ourui_Pallette,0);
        b2=laBeginRow(uil,c,0,0);
        laShowItem(uil,c,0,"OUR_pallette_new_color")->Expand=1;
        laUiList* muil=laMakeMenuPage(uil,c,"☰"); laColumn* mc=laFirstColumn(muil);{
            laShowItem(muil,mc,0,"OUR_remove_pallette");
        }
        laEndRow(uil,b2);
    }laElse(uil,b1);{
        laShowItem(uil,c,0,"OUR_new_pallette")->Expand=1;
        laEndRow(uil,b);
    }laEndCondition(uil,b1);
}
void ourui_BrushPage(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laColumn* cl,*cr; laSplitColumn(uil,c,0.5); cl=laLeftColumn(c,15);cr=laRightColumn(c,0);
    
    laUiItem*row=laBeginRow(uil,cr,0,0);
    laShowSeparator(uil,cr)->Expand=1;
    laShowItemFull(uil, cr, 0, "LA_open_internet_link", 0, "icon=📖;link=http://www.ChengduLittleA.com/ourpaintnodeshelp;text=Nodes Help", 0, 0);
    laEndRow(uil,row);

    laShowItemFull(uil,cl,0,"our.tools.current_brush",LA_WIDGET_COLLECTION_SELECTOR,0,laui_IdentifierOnly,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;
    laUiItem* b=laOnConditionThat(uil,c,laPropExpression(0,"our.tools.current_brush"));{
        laShowItemFull(uil,c,0,"our.tools.current_brush.rack_page",LA_WIDGET_COLLECTION_SINGLE,0,0,0)->Flags|=LA_UI_FLAGS_NO_DECAL|LA_UI_FLAGS_NO_CONFIRM;
    }laEndCondition(uil,b);
}
void ourui_AboutAuthor(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laUiItem* g; laUiList* gu; laColumn* gc;
    g = laMakeGroup(uil, c, "Our Paint", 0);
    gu = g->Page;{ gc = laFirstColumn(gu);
        laShowLabel(gu,gc,"Our Paint is made by Wu Yiming.",0,0)->Flags|=LA_TEXT_LINE_WRAP;
        laUiItem* b =laBeginRow(gu,gc,0,0);
        laShowItemFull(gu, gc, 0, "LA_open_internet_link", 0, "link=http://www.ChengduLittleA.com/ourpaint;text=Our Paint blog", 0, 0);
        laShowItemFull(gu, gc, 0, "LA_open_internet_link", 0, "link=http://www.ChengduLittleA.com/ourpaintlog;text=Dev log", 0, 0);
        laEndRow(gu,b);
        b=laBeginRow(gu,gc,0,0);
        laShowItemFull(gu, gc, 0, "LA_open_internet_link", 0, "icon=$;link=https://www.patreon.com/chengdulittlea;text=Donate", 0, 0);
        laShowItemFull(gu, gc, 0, "LA_open_internet_link", 0, "icon=￥;link=http://www.ChengduLittleA.com/donate;text=Donate (China)", 0, 0);
        laEndRow(gu,b);
    }
    g = laMakeGroup(uil, c, "Credits to Sponsors", 0);
    gu = g->Page;{ gc = laFirstColumn(gu);
        laShowLabel(gu,gc,"- Deathblood",0,0);
        laShowLabel(gu,gc,"- Leone Arturo",0,0);
        laShowLabel(gu,gc,"- 贵州混混",0,0);
        laShowLabel(gu,gc,"- Louis Lithium",0,0);
        laShowLabel(gu,gc,"- Nayeli Lafeuille",0,0);
        laShowLabel(gu,gc,"- Ibrahim Lawai",0,0);
        laShowLabel(gu,gc,"- Jacob Curtis",0,0);
    }
}
void ourui_AboutVersion(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); laUiItem* g; laUiList* gu; laColumn* gc;
    g = laMakeGroup(uil, c, "Our Paint", 0);
    gu = g->Page;{
        gc = laFirstColumn(gu); char buf[128]; sprintf(buf,"Our Paint %d.%d [%d]",OUR_VERSION_MAJOR,OUR_VERSION_MINOR,OUR_VERSION_SUB);
        laShowLabel(gu,gc,buf,0,0)->Flags|=LA_TEXT_MONO;
        laShowLabel(gu, gc, OURPAINT_GIT_BRANCH,0,0)->Flags|=LA_TEXT_MONO;
#ifdef OURPAINT_GIT_HASH
        laShowLabel(gu, gc, OURPAINT_GIT_HASH,0,0)->Flags|=LA_TEXT_MONO;
#endif
        laShowLabel(gu, gc, "Single canvas implementation.", 0, 0)->Flags|=LA_TEXT_MONO|LA_TEXT_LINE_WRAP;
    }
}
void ourui_AboutContent(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); 

    laShowLabel(uil, c, "Our Paint", 0, 0);
    laShowLabel(uil, c, "A simple yet flexible node-based GPU painting program.", 0, 0)->Flags|=LA_TEXT_LINE_WRAP;
    laShowLabel(uil, c, "(C)Yiming Wu", 0, 0);
}
void ourui_OurPreference(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c = laFirstColumn(uil),*cl,*cr; laSplitColumn(uil,c,0.5);cl=laLeftColumn(c,0);cr=laRightColumn(c,0);
    laUiItem* b,*uiitem;

    laShowLabel(uil,c,"Generic:",0,0);
    uiitem=laShowItem(uil,cl,0,"our.preferences.enable_brush_circle");
    b=laOnConditionThat(uil,cr,laPropExpression(&uiitem->PP,""));
    laShowLabel(uil,cr,"Show brush direction:",0,0);
    laShowItem(uil,cr,0,"our.preferences.brush_circle_tilt_mode")->Flags|=LA_UI_FLAGS_EXPAND;
    laEndCondition(uil,b);
    laShowItem(uil,cl,0,"our.preferences.spectral_mode");

    laShowSeparator(uil,c);

    laShowLabel(uil,c,"Pressure:",0,0);
    laShowItem(uil,cl,0,"our.preferences.allow_none_pressure");
    laShowItem(uil,cr,0,"our.preferences.bad_event_tolerance");
    laShowItem(uil,cl,0,"our.preferences.smoothness");
    laShowItem(uil,cr,0,"our.preferences.hardness");

    laShowSeparator(uil,c);

    laShowLabel(uil,c,"Canvas:",0,0);
    laShowItem(uil,cl,0,"our.preferences.show_stripes");
    laShowItem(uil,cr,0,"our.preferences.canvas_default_scale");
    laShowItem(uil,cl,0,"our.preferences.show_grid");
    laShowItem(uil,cr,0,"our.preferences.multithread_write");
    
    laShowSeparator(uil,c);

    laShowLabel(uil,c,"Shortcut Buttons:",0,0);
    laShowItem(uil,cl,0,"our.preferences.undo_on_header");
    laShowItem(uil,cr,0,"our.preferences.tools_on_header");
    laShowItem(uil,cl,0,"our.preferences.mix_mode_on_header");
    laShowItem(uil,cr,0,"our.preferences.brush_numbers_on_header");

    laShowSeparator(uil,c);

    laShowLabel(uil,c,"Undo:",0,0);
    laShowItem(uil,c,0,"our.preferences.paint_undo_limit");
    
    laShowSeparator(uil,c);

    laShowLabel(uil,c,"Exporting Defaults:",0,0);
    laShowLabel(uil,cl,"Bit Depth:",0,0); laShowItem(uil,cr,0,"our.preferences.export_default_bit_depth");
    laShowLabel(uil,cl,"Color Profile:",0,0); laShowItem(uil,cr,0,"our.preferences.export_default_color_profile");
    
    laShowSeparator(uil,c);

#ifdef LA_LINUX

    laShowLabel(uil,c,"System:",0,0);
    laShowItem(uil,cl,0,"OUR_register_file_associations");
    b=laOnConditionThat(uil,cr,laPropExpression(0,"our.preferences.file_registered"));{
        laShowLabel(uil,cr,"Registered",0,0)->Flags|=LA_UI_FLAGS_DISABLED;
    }laElse(uil,b);{
        laShowLabel(uil,cr,"Not registered",0,0)->Flags|=LA_UI_FLAGS_HIGHLIGHT;
    }laEndCondition(uil,b);

    laShowSeparator(uil,c);

#endif

    laShowLabel(uil,c,"Developer:",0,0);
    laShowItem(uil,cl,0,"our.preferences.show_debug_tiles");
}
void ourui_SplashPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c = laFirstColumn(uil),*cl,*cr; laSplitColumn(uil,c,0.5);cl=laLeftColumn(c,0);cr=laRightColumn(c,0);
    if(MAIN.CurrentWindow->CW>2500){
        laShowImage(uil,c,Our->SplashImageHigh,5)->Flags|=LA_UI_IMAGE_FULL_W;
    }else{
        laShowImage(uil,c,Our->SplashImage,5)->Flags|=LA_UI_IMAGE_FULL_W;
    }
    laUiItem* b=laBeginRow(uil,cl,0,0); laShowLabel(uil,cl,OUR_PAINT_NAME_STRING,0,0);
    laShowItemFull(uil, cl, 0, "LA_open_internet_link", 0, "icon=★;link=https://www.wellobserve.com/index.php?post=20240421171033;text=Release Notes", 0, 0);
    laEndRow(uil,b);
    laShowLabel(uil,cl,"Our Paint is a free application.",0,0)->Flags|=LA_UI_FLAGS_DISABLED|LA_TEXT_LINE_WRAP|LA_UI_MIN_WIDTH;
    b=laBeginRow(uil,cl,0,0);
    laShowLabel(uil, cl, OURPAINT_GIT_BRANCH,0,0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
#ifdef OURPAINT_GIT_HASH
    laShowLabel(uil, cl, OURPAINT_GIT_HASH,0,0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
#endif
    laEndRow(uil,b);
    laShowLabel(uil,cl," ",0,0);
    
    laShowItemFull(uil, cr, 0, "LA_open_internet_link", 0, "icon=🗩;link=http://www.ChengduLittleA.com/ourpaint;text=Our Paint blog", 0, 0);
    laShowItemFull(uil, cr, 0, "LA_open_internet_link", 0, "link=http://www.ChengduLittleA.com/ourpaintlog;text=Development logs", 0, 0);
    laShowItemFull(uil, cr, 0, "LA_open_internet_link", 0, "icon=📖;link=http://www.ChengduLittleA.com/ourpaintmanual;text=User Manual", 0, 0);
    laShowItemFull(uil, cr, 0, "LA_open_internet_link", 0, "icon=🐞;link=https://www.wellobserve.com/repositories/chengdulittlea/OurPaint/issues;text=Report a Bug", 0, 0);
    laShowLabel(uil,cr," ",0,0);
    laShowLabel(uil,cr,"Support the development:",0,0)->Flags|=LA_UI_FLAGS_DISABLED|LA_TEXT_LINE_WRAP|LA_UI_MIN_WIDTH;
    b=laBeginRow(uil,cr,1,0);
    laShowItemFull(uil, cr, 0, "LA_open_internet_link", 0, "icon=$;link=https://www.patreon.com/chengdulittlea;text=Donate", 0, 0);
    laShowItemFull(uil, cr, 0, "LA_open_internet_link", 0, "icon=￥;link=http://www.ChengduLittleA.com/donate;text=Donate (China)", 0, 0);
    laEndRow(uil,b);

    laShowLabel(uil,cl,"Cover artist:",0,0)->Flags|=LA_UI_FLAGS_DISABLED;
    b=laBeginRow(uil,cl,0,0);
    laShowLabel(uil,cl,"吴奕茗 Wu Yiming",0,0);
    laShowItemFull(uil, cl, 0, "LA_open_internet_link", 0, "text=Website;link=http://www.ChengduLittleA.com", 0, 0);
    laEndRow(uil,b);

    laShowLabel(uil,cl," ",0,0);
    b=laBeginRow(uil,cl,0,0);
    laShowLabel(uil, cl, "语言/Language",0,0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
    laShowItemFull(uil, cl, 0, "la.user_preferences.enable_translation",LA_WIDGET_ENUM_HIGHLIGHT,"text=翻译/Translate",0,0);
    laEndRow(uil,b);
    laUiItem* b1=laOnConditionThat(uil, cl, laPropExpression(0, "la.user_preferences.enable_translation"));
    laShowItemFull(uil, cl, 0, "la.user_preferences.languages",LA_WIDGET_COLLECTION_SELECTOR,0,0,0);
    laEndCondition(uil,b1);
}
void our_EnableSplashPanel(){
    laEnableSplashPanel(ourui_SplashPanel,0,100,0,2000,1500,0);
}

void our_CanvasDrawTextures(){
    tnsUseImmShader; tnsEnableShaderv(T->immShader); real MultiplyColor[4];
    for(OurLayer* l=Our->Layers.pLast;l;l=l->Item.pPrev){
        if(l->Hide || l->Transparency==1) continue; real a=1-l->Transparency;
        if(Our->SketchMode && l->AsSketch){
            if(Our->SketchMode == 1){ a=1.0f; }
            elif(Our->SketchMode == 2){ a=0.0f; }
        }
        tnsVectorSet4(MultiplyColor,a,a,a,a); int any=0; 
        if(l->BlendMode==OUR_BLEND_NORMAL){ glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA); }
        if(l->BlendMode==OUR_BLEND_ADD){ glBlendFunc(GL_ONE,GL_ONE); }
        for(int row=0;row<OUR_TILES_PER_ROW;row++){
            if(!l->TexTiles[row]) continue;
            for(int col=0;col<OUR_TILES_PER_ROW;col++){
                if(!l->TexTiles[row][col] || !l->TexTiles[row][col]->Texture) continue;
                int sx=l->TexTiles[row][col]->l,sy=l->TexTiles[row][col]->b;
                real pad=(real)OUR_TILE_SEAM/OUR_TILE_W; int seam=OUR_TILE_SEAM;
                tnsDraw2DTextureArg(l->TexTiles[row][col]->Texture,sx+seam,sy+OUR_TILE_W-seam,OUR_TILE_W-seam*2,-OUR_TILE_W+seam*2,MultiplyColor,pad,pad,pad,pad);
                any=1;
            }
        }
        if(any) tnsFlush();
    }
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
}
void our_CanvasDrawTiles(){
    OurLayer* l=Our->CurrentLayer; if(!l) return;
    tnsUseImmShader; tnsEnableShaderv(T->immShader); tnsUniformUseTexture(T->immShader,0,0); tnsUseNoTexture();
    int any=0;
    for(int row=0;row<OUR_TILES_PER_ROW;row++){
        if(!l->TexTiles[row]) continue;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){
            if(!l->TexTiles[row][col] || !l->TexTiles[row][col]->Texture) continue;
            int sx=l->TexTiles[row][col]->l,sy=l->TexTiles[row][col]->b;
            //tnsVertex2d(sx, sy); tnsVertex2d(sx+OUR_TILE_W,sy);
            //tnsVertex2d(sx+OUR_TILE_W, sy+OUR_TILE_W); tnsVertex2d(sx,sy+OUR_TILE_W);
            //tnsColor4dv(laAccentColor(LA_BT_NORMAL));
            //tnsPackAs(GL_TRIANGLE_FAN);
            tnsVertex2d(sx, sy); tnsVertex2d(sx+OUR_TILE_W,sy);
            tnsVertex2d(sx+OUR_TILE_W, sy+OUR_TILE_W); tnsVertex2d(sx,sy+OUR_TILE_W);
            tnsColor4dv(laAccentColor(LA_BT_TEXT));
            tnsPackAs(GL_LINE_LOOP);    
        }
    }
    if(any) tnsFlush();
}
void our_CanvasDrawCropping(OurCanvasDraw* ocd){
    tnsUseImmShader(); tnsEnableShaderv(T->immShader); tnsUniformUseTexture(T->immShader,0,0); tnsUseNoTexture();
    if(Our->BorderFadeWidth > 1e-6){
        real _H=Our->H,_W=Our->W,_X=Our->X,_Y=Our->Y-Our->H;
        real color[72]={0}; for(int i=1;i<18;i++){ color[i*4+3]=Our->BorderAlpha; }
        real r=TNS_MIN2(Our->W,Our->H)/2.0f * Our->BorderFadeWidth;
        real pos[36];
        pos[0]=_X+_W-r;pos[1]=_Y+_H-r;
        tnsMakeArc2d(&pos[2],16,pos[0],pos[1],2*r,0,rad(90));
        tnsVertexArray2d(pos,18); tnsColorArray4d(color,18); tnsPackAs(GL_TRIANGLE_FAN);
        pos[0]=1e6;pos[1]=1e6; tnsColor4d(0,0,0,Our->BorderAlpha); tnsVertexArray2d(pos,18); tnsPackAs(GL_TRIANGLE_FAN);
        pos[0]=_X+r;pos[1]=_Y+_H-r;
        tnsMakeArc2d(&pos[2],16,pos[0],pos[1],2*r,rad(90),rad(180));
        tnsVertexArray2d(pos,18); tnsColorArray4d(color,18); tnsPackAs(GL_TRIANGLE_FAN);
        pos[0]=-1e6;pos[1]=1e6; tnsColor4d(0,0,0,Our->BorderAlpha); tnsVertexArray2d(pos,18); tnsPackAs(GL_TRIANGLE_FAN);
        pos[0]=_X+r;pos[1]=_Y+r;
        tnsMakeArc2d(&pos[2],16,pos[0],pos[1],2*r,rad(180),rad(270));
        tnsVertexArray2d(pos,18); tnsColorArray4d(color,18); tnsPackAs(GL_TRIANGLE_FAN);
        pos[0]=-1e6;pos[1]=-1e6; tnsColor4d(0,0,0,Our->BorderAlpha); tnsVertexArray2d(pos,18); tnsPackAs(GL_TRIANGLE_FAN);
        pos[0]=_X+_W-r;pos[1]=_Y+r;
        tnsMakeArc2d(&pos[2],16,pos[0],pos[1],2*r,rad(270),rad(360));
        tnsVertexArray2d(pos,18); tnsColorArray4d(color,18); tnsPackAs(GL_TRIANGLE_FAN);
        pos[0]=1e6;pos[1]=-1e6; tnsColor4d(0,0,0,Our->BorderAlpha); tnsVertexArray2d(pos,18); tnsPackAs(GL_TRIANGLE_FAN);
        
        real color1[16]={0}; color1[7]=color1[11]=Our->BorderAlpha;
        tnsVertex2d(_X+_W-r,_Y+_H-r); tnsVertex2d(_X+_W-r,_Y+_H+r);
        tnsVertex2d(_X+r,_Y+_H+r); tnsVertex2d(_X+r,_Y+_H-r);
        tnsColorArray4d(color1,4); tnsPackAs(GL_TRIANGLE_FAN);
        tnsVertex2d(_X+r,_Y+_H-r); tnsVertex2d(_X-r,_Y+_H-r);
        tnsVertex2d(_X-r,_Y+r); tnsVertex2d(_X+r,_Y+r);
        tnsColorArray4d(color1,4); tnsPackAs(GL_TRIANGLE_FAN);
        tnsVertex2d(_X+r,_Y+r); tnsVertex2d(_X+r,_Y-r);
        tnsVertex2d(_X+_W-r,_Y-r); tnsVertex2d(_X+_W-r,_Y+r);
        tnsColorArray4d(color1,4); tnsPackAs(GL_TRIANGLE_FAN);
        tnsVertex2d(_X+_W-r,_Y+r); tnsVertex2d(_X+_W+r,_Y+r);
        tnsVertex2d(_X+_W+r,_Y+_H-r); tnsVertex2d(_X+_W-r,_Y+_H-r);
        tnsColorArray4d(color1,4); tnsPackAs(GL_TRIANGLE_FAN);

        tnsColor4d(0,0,0,Our->BorderAlpha);
        tnsVertex2d(_X+_W-r,_Y+_H+r);tnsVertex2d(1e6,1e6);
        tnsVertex2d(-1e6,1e6);tnsVertex2d(_X+r,_Y+_H+r);
        tnsPackAs(GL_TRIANGLE_FAN);
        tnsVertex2d(_X-r,_Y+_H-r);tnsVertex2d(-1e6,1e6);
        tnsVertex2d(-1e6,-1e6); tnsVertex2d(_X-r,_Y+r);
        tnsPackAs(GL_TRIANGLE_FAN);
        tnsVertex2d(_X+r,_Y-r);tnsVertex2d(-1e6,-1e6);
        tnsVertex2d(1e6,-1e6); tnsVertex2d(_X+_W-r,_Y-r);
        tnsPackAs(GL_TRIANGLE_FAN);
        tnsVertex2d(_X+_W+r,_Y+r);tnsVertex2d(1e6,-1e6);
        tnsVertex2d(1e6,1e6); tnsVertex2d(_X+_W+r,_Y+_H-r);
        tnsPackAs(GL_TRIANGLE_FAN);

        tnsFlush();
        return;
    }

    tnsColor4d(0,0,0,Our->BorderAlpha);
    tnsVertex2d(-1e6,Our->Y); tnsVertex2d(1e6,Our->Y); tnsVertex2d(-1e6,1e6); tnsVertex2d(1e6,1e6); tnsPackAs(GL_TRIANGLE_FAN);
    tnsVertex2d(-1e6,Our->Y); tnsVertex2d(Our->X,Our->Y); tnsVertex2d(Our->X,Our->Y-Our->H); tnsVertex2d(-1e6,Our->Y-Our->H); tnsPackAs(GL_TRIANGLE_FAN);
    tnsVertex2d(1e6,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y-Our->H); tnsVertex2d(1e6,Our->Y-Our->H); tnsPackAs(GL_TRIANGLE_FAN);
    tnsVertex2d(-1e6,Our->Y-Our->H); tnsVertex2d(1e6,Our->Y-Our->H); tnsVertex2d(-1e6,-1e6); tnsVertex2d(1e6,-1e6); tnsPackAs(GL_TRIANGLE_FAN);

    if(Our->Tool==OUR_TOOL_CROP){
        tnsColor4dv(laAccentColor(LA_BT_TEXT));
        tnsVertex2d(Our->X,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y); tnsVertex2d(Our->X+Our->W,Our->Y-Our->H); tnsVertex2d(Our->X,Our->Y-Our->H);
        tnsLineWidth(3); tnsPackAs(GL_LINE_LOOP);
        tnsLineWidth(1); tnsFlush(); 
    }
}
void our_CanvasGetRefString(char* ref){
    int refs=Our->RefSize; int add=0; if(Our->ShowRef==2){ refs++; add=1; }
    if(Our->RefCategory==0){ sprintf(ref,"%sA%d",add?"2X":"",refs); }
    elif(Our->RefCategory==1){ sprintf(ref,"%sB%d",add?"2X":"",refs); }
    elif(Our->RefCategory==2){ sprintf(ref,"%s%dK",add?"2X":"",refs?((int)pow(2,refs-1)):0); }
}
#define OUR_GET_REF_SIZE(W,H) \
    { if(Our->RefCategory==0){ W=118.9,H=84.1; } \
    elif(Our->RefCategory==1){ W=141.4,H=100.0; } \
    elif(Our->RefCategory==2){ W=109.2,H=78.7; } \
    for(int i=0;i<Our->RefSize;i++){ if(W>H){ W/=2; }else{ H/=2; } } \
    if((Our->RefOrientation && (W>H))||((!Our->RefOrientation) && (W<H))){ real t=H; H=W; W=t; } }
void our_CanvasDrawReferenceBlock(OurCanvasDraw* ocd){
    real W,H,W2,H2; char str[128]; our_CanvasGetRefString(str);
    OUR_GET_REF_SIZE(W,H); sprintf(str+strlen(str)," %dX%.dmm",(int)W*10,(int)H*10);
    real dpc=OUR_DPC; W*=dpc; H*=dpc; W2=W/2; H2=H/2;
    real LM=Our->RefMargins[0]*dpc,RM=LM,TM=Our->RefMargins[1]*dpc,BM=TM;
    real LP=Our->RefPaddings[0]*dpc,RP=LP,TP=Our->RefPaddings[1]*dpc,BP=TP;
    real MM=Our->RefMargins[2]*dpc;

    tnsUseImmShader; tnsEnableShaderv(T->immShader); tnsUniformUseTexture(T->immShader,0,0); tnsUseNoTexture();
    tnsColor4d(0,0,0,Our->RefAlpha); tnsLineWidth(3.0);
    tnsVertex2d(-W2,H2); tnsVertex2d(W2,H2); tnsVertex2d(W2,-H2); tnsVertex2d(-W2,-H2); tnsPackAs(GL_LINE_LOOP);
    if(Our->ShowRef==2){
        if(Our->RefOrientation){ tnsVertex2d(W2,0); tnsVertex2d(-W2,0); }
        else{ tnsVertex2d(0,H2); tnsVertex2d(0,-H2); }tnsPackAs(GL_LINES);
    }
    tnsColor4d(0,0,0,Our->RefAlpha*0.6); tnsLineWidth(1.0);
    tnsVertex2d(-W2+LM,H2-TM); tnsVertex2d(W2-LM,H2-TM); tnsVertex2d(W2-LM,-H2+BM); tnsVertex2d(-W2+LM,-H2+BM); tnsPackAs(GL_LINE_LOOP);
    tnsVertex2d(-W2-LP,H2+TP); tnsVertex2d(W2+LP,H2+TP); tnsVertex2d(W2+LP,-H2-BP); tnsVertex2d(-W2-LP,-H2-BP); tnsPackAs(GL_LINE_LOOP);
    if(Our->ShowRef==2){
        if(Our->RefOrientation){ tnsVertex2d(W2,-MM); tnsVertex2d(-W2,-MM); tnsVertex2d(W2,MM); tnsVertex2d(-W2,MM);  }
        else{ tnsVertex2d(-MM,H2); tnsVertex2d(-MM,-H2); tnsVertex2d(MM,H2); tnsVertex2d(MM,-H2); }
        tnsPackAs(GL_LINES);
    }

    real tcolor[4]={0,0,0,Our->RefAlpha}; real th=ocd->Base.ZoomX*1.5;
    tnsLineWidth(3);
    tnsDrawStringLCD(str,0,tcolor,-W2,W2,H2+th*LA_RH,LA_TEXT_LCD_16|LA_TEXT_REVERT_Y,th);
    tnsLineWidth(1);
    tnsFlush();
}
void our_CanvasDrawBrushCircle(OurCanvasDraw* ocd){
    real colorw[4]={1,1,1,0.5}; real colork[4]={0,0,0,0.5};
    if(Our->Tool==OUR_TOOL_MOVE || (Our->Tool==OUR_TOOL_CROP && Our->ShowBorder)){
        tnsUseImmShader();
        tnsDrawStringM("🤚",0,colork,ocd->Base.OnX-LA_RH,ocd->Base.OnX+10000,ocd->Base.OnY-LA_RH,0);
        tnsDrawStringM("🤚",0,colorw,ocd->Base.OnX-2-LA_RH,ocd->Base.OnX+10000,ocd->Base.OnY-2-LA_RH,0);
        return;
    }
    real v[96]; real Radius=OUR_BRUSH_ACTUAL_SIZE(Our->CurrentBrush)/ocd->Base.ZoomX, gap=rad(2);
    tnsUseImmShader();tnsUseNoTexture(); tnsLineWidth(1.5);
    OurLayer* l = Our->CurrentLayer;
    if (!Our->CurrentBrush || !l || l->Hide || l->Transparency==1 || l->Lock ||
        (l->AsSketch && Our->SketchMode==2)|| ocd->Base.SelectThrough || (Our->Tool==OUR_TOOL_CROP && !Our->ShowBorder)){
        real d = Radius * 0.707;
        tnsColor4d(0,0,0,0.5);
        tnsVertex2d(ocd->Base.OnX-d+1, ocd->Base.OnY+d-1); tnsVertex2d(ocd->Base.OnX+d+1, ocd->Base.OnY-d-1);
        tnsVertex2d(ocd->Base.OnX-d+1, ocd->Base.OnY-d-1); tnsVertex2d(ocd->Base.OnX+d+1, ocd->Base.OnY+d-1);
        tnsPackAs(GL_LINES);
        tnsColor4d(1,1,1,0.5);
        tnsVertex2d(ocd->Base.OnX-d, ocd->Base.OnY+d-1); tnsVertex2d(ocd->Base.OnX+d, ocd->Base.OnY-d-1);
        tnsVertex2d(ocd->Base.OnX-d, ocd->Base.OnY-d-1); tnsVertex2d(ocd->Base.OnX+d, ocd->Base.OnY+d-1);
        tnsPackAs(GL_LINES);
        tnsLineWidth(1.0);
        return;
    }
    if(Our->ShowBrushName){
        tnsDrawStringAuto(SSTR(Our->CurrentBrush->Name),colork,ocd->Base.OnX-10000,ocd->Base.OnX-LA_RH,ocd->Base.OnY-LA_RH,LA_TEXT_ALIGN_RIGHT);
        tnsDrawStringAuto(SSTR(Our->CurrentBrush->Name),colorw,ocd->Base.OnX-10000,ocd->Base.OnX-LA_RH-2,ocd->Base.OnY-2-LA_RH,LA_TEXT_ALIGN_RIGHT);
        tnsUseNoTexture();
    }
    if(Our->ShowBrushNumber){
        char buf[32]; if(Our->BrushNumber){ sprintf(buf,"#%d",Our->BrushNumber-1); }else{ sprintf(buf,"[%.1lf]",OUR_BRUSH_ACTUAL_SIZE(Our->CurrentBrush)); }
        tnsDrawStringAuto(buf,colork,ocd->Base.OnX-10000,ocd->Base.OnX-LA_RH,ocd->Base.OnY,LA_TEXT_ALIGN_RIGHT);
        tnsDrawStringAuto(buf,colorw,ocd->Base.OnX-10000,ocd->Base.OnX-LA_RH-2,ocd->Base.OnY-2,LA_TEXT_ALIGN_RIGHT);
        tnsUseNoTexture();
    }
    tnsMakeCircle2d(v,48,ocd->Base.OnX,ocd->Base.OnY,Radius+0.5,0);
    tnsColor4d(1,1,1,0.5); tnsVertexArray2d(v,48); tnsPackAs(GL_LINE_LOOP);
    tnsMakeCircle2d(v,48,ocd->Base.OnX,ocd->Base.OnY,Radius-0.5,0);
    tnsColor4d(0,0,0,0.5); tnsVertexArray2d(v,48); tnsPackAs(GL_LINE_LOOP);
    real brush_angle = 0;
    switch(Our->BrushCircleTiltMode){
    case 0: default: break;
    case 1: brush_angle = -Our->EventTiltOrientation; break;
    case 2: brush_angle = Our->EventTwistAngle; break;
    case 3: brush_angle = Our->EventHasTwist?Our->EventTwistAngle:-Our->EventTiltOrientation; break;
    }
    if(Our->BrushCircleTiltMode){
        tnsColor4d(0,0,0,0.5);
        tnsVertex2d(ocd->Base.OnX+sin(brush_angle+gap)*Radius,ocd->Base.OnY+cos(brush_angle+gap)*Radius);
        tnsVertex2d(ocd->Base.OnX-sin(brush_angle-gap)*Radius,ocd->Base.OnY-cos(brush_angle-gap)*Radius);
        tnsVertex2d(ocd->Base.OnX+sin(brush_angle-gap)*Radius,ocd->Base.OnY+cos(brush_angle-gap)*Radius);
        tnsVertex2d(ocd->Base.OnX-sin(brush_angle+gap)*Radius,ocd->Base.OnY-cos(brush_angle+gap)*Radius);
        tnsPackAs(GL_LINES);
        tnsColor4d(1,1,1,0.5);
        tnsVertex2d(ocd->Base.OnX+sin(brush_angle)*Radius,ocd->Base.OnY+cos(brush_angle)*Radius);
        tnsVertex2d(ocd->Base.OnX-sin(brush_angle)*Radius,ocd->Base.OnY-cos(brush_angle)*Radius);
        tnsPackAs(GL_LINES);
    }
    if(Our->CurrentBrush && Our->CurrentBrush->VisualOffset > 1e-4){
        tnsMakeCircle2d(v,48,ocd->PointerX,ocd->PointerY,Radius/4+0.5,0);
        tnsColor4d(1,1,1,0.5); tnsVertexArray2d(v,48); tnsPackAs(GL_LINE_LOOP);
        tnsMakeCircle2d(v,48,ocd->PointerX,ocd->PointerY,Radius/4-0.5,0);
        tnsColor4d(0,0,0,0.5); tnsVertexArray2d(v,48); tnsPackAs(GL_LINE_LOOP);
        tnsVertex2d(ocd->PointerX,ocd->PointerY);
        tnsVertex2d(ocd->Base.OnX,ocd->Base.OnY);
        real vcolor[8]={1,1,1,0.3,0,0,0,0.5};
        tnsColorArray4d(vcolor,2); tnsPackAs(GL_LINES);
    }
    tnsLineWidth(1.0);
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

    logPrintNew("Our Paint initialization:\n");

    int work_grp_cnt[3];
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 0, &work_grp_cnt[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 1, &work_grp_cnt[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, 2, &work_grp_cnt[2]);
    logPrint("GPU max global (total) work group counts x:%i y:%i z:%i\n", work_grp_cnt[0], work_grp_cnt[1], work_grp_cnt[2]);

    int work_grp_size[3];
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 0, &work_grp_size[0]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 1, &work_grp_size[1]);
    glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_SIZE, 2, &work_grp_size[2]);
    logPrint("GPU max local (in one shader) work group sizes x:%i y:%i z:%i\n", work_grp_size[0], work_grp_size[1], work_grp_size[2]);

    int work_grp_inv;
    glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, &work_grp_inv);
    logPrint("GPU max local work group invocations %i\n", work_grp_inv);
}
void our_CanvasDrawCanvas(laBoxedTheme *bt, OurPaint *unused_c, laUiItem* ui){
    OurCanvasDraw* ocd=ui->Extra; OurPaint* oc=ui->PP.EndInstance; laCanvasExtra*e=&ocd->Base;
    int W, H; W = ui->R - ui->L; H = ui->B - ui->U;
    tnsFlush();

    if (!e->OffScr || e->OffScr->pColor[0]->Height != H || e->OffScr->pColor[0]->Width != W){
        if (e->OffScr) tnsDelete2DOffscreen(e->OffScr);
        e->OffScr = tnsCreate2DOffscreen(GL_RGBA16F, W, H, 0, 0, 0);
    }

    //our_CANVAS_TEST(bt,ui);
    //glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE);
    tnsUseImmShader(); tnsEnableShaderv(T->immShader); tnsUniformColorMode(T->immShader,0);
    tnsUniformOutputColorSpace(T->immShader, 0); tnsUniformColorComposing(T->immShader,0,0,0,0);

    tnsDrawToOffscreen(e->OffScr,1,0);
    tnsViewportWithScissor(0, 0, W, H);
    tnsResetViewMatrix();tnsResetModelMatrix();tnsResetProjectionMatrix();
    tnsOrtho(e->PanX - W * e->ZoomX / 2, e->PanX + W * e->ZoomX / 2, e->PanY - e->ZoomY * H / 2, e->PanY + e->ZoomY * H / 2, 100, -100);
    tnsClearColor(LA_COLOR3(Our->BackgroundColor),1); tnsClearAll();
    if(Our->ShowTiles){ our_CanvasDrawTiles(); }
    our_CanvasDrawTextures();
    if(Our->ShowBorder){ our_CanvasDrawCropping(ocd); }
    if(Our->ShowRef){ our_CanvasDrawReferenceBlock(ocd); }
}
void our_CanvasDrawOverlay(laUiItem* ui,int h){
    laCanvasExtra *e = ui->Extra; OurCanvasDraw* ocd=e;
    laBoxedTheme *bt = (*ui->Type->Theme);

    tnsUseImmShader(); tnsEnableShaderv(T->immShader); tnsUniformColorMode(T->immShader,2);
    tnsUniformOutputColorSpace(T->immShader, 0); tnsUniformColorComposing(T->immShader,0,0,0,0);
    if(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_SRGB){ tnsUniformInputColorSpace(T->immShader, 0); }
    elif(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_CLAY){ tnsUniformInputColorSpace(T->immShader, 1); }
    elif(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_D65_P3){ tnsUniformInputColorSpace(T->immShader, 2); }

    tnsDraw2DTextureDirectly(e->OffScr->pColor[0], ui->L, ui->U, ui->R - ui->L, ui->B - ui->U);
    tnsFlush();

    tnsUniformColorMode(T->immShader, 0); tnsUniformInputColorSpace(T->immShader, 0);
    laWindow* w=MAIN.CurrentWindow;
    tnsUniformOutputColorSpace(T->immShader, w->OutputColorSpace);
    tnsUniformColorComposing(T->immShader,w->UseComposing,w->ComposingGamma,w->ComposingBlackpoint,w->OutputProofing);
    if(Our->EnableBrushCircle && (!ocd->HideBrushCircle)){ our_CanvasDrawBrushCircle(ocd); }

    if(!(ui->Flags&LA_UI_FLAGS_NO_OVERLAY)){
        real colorw[4]={1,1,1,0.5}; real colork[4]={0,0,0,0.5};
        if(Our->ShowStripes){ int UH=TNS_MIN2(LA_RH,(ui->B-ui->U)/8); real varr[8]; real carr[16];
            tnsUseNoTexture();
            tnsVectorSet4(&varr[0], ui->L,ui->B-UH,ui->R,ui->B-UH);
            tnsVectorSet4(&varr[4], ui->R,ui->B-2*UH,ui->L,ui->B-2*UH);
            tnsVectorSet4(&carr[0], 0,0,0,1); tnsVectorSet4(&carr[4], 1,1,1,1);
            tnsVectorSet4(&carr[8], 1,1,1,1); tnsVectorSet4(&carr[12], 0,0,0,1);
            tnsVertexArray2d(varr,4); tnsColorArray4d(carr,4);
            tnsPackAs(GL_TRIANGLE_FAN);
            tnsVertex2d(ui->L,ui->B); tnsVertex2d(ui->R,ui->B); tnsVertex2d(ui->R,ui->B-UH); tnsVertex2d(ui->L,ui->B-UH);
            tnsColor4d(0,0,0,1); tnsPackAs(GL_TRIANGLE_FAN);
            tnsVertex2d(ui->L,ui->B-UH*2); tnsVertex2d(ui->R,ui->B-UH*2); tnsVertex2d(ui->R,ui->B-UH*3); tnsVertex2d(ui->L,ui->B-UH*3);
            tnsColor4d(1,1,1,1); tnsPackAs(GL_TRIANGLE_FAN);
            real ca[16]={0,0,0,1,0.5,0.5,0.5,1,1,1,1,1,0.5,0.5,0.5,1};
            int count=(ui->R-ui->L)/UH; real sp=(real)(ui->R-ui->L)/(real)count;
            for(int i=0;i<count;i++){ real sl=sp*i+ui->L;
                tnsVertex2d(sl,ui->U); tnsVertex2d(sl+sp,ui->U); tnsVertex2d(sl+sp,ui->U+UH); tnsVertex2d(sl,ui->U+UH);
                tnsColor4dv(&ca[(i%4)*4]); tnsPackAs(GL_TRIANGLE_FAN);
            }
        }
        if(Our->ShowGrid){
            tnsUseNoTexture();
            int delta=LA_RH*1.5; if(delta<15){delta=15;} int c=0;
            for(int i=ui->L+delta;i<ui->R;i+=delta*2){ tnsVertex2d(i,ui->B); tnsVertex2d(i,ui->U); } tnsColor4d(0,0,0,0.5); tnsPackAs(GL_LINES);
            for(int i=ui->L+delta*2;i<ui->R;i+=delta*2){ tnsVertex2d(i,ui->B); tnsVertex2d(i,ui->U); } tnsColor4d(1,1,1,0.5); tnsPackAs(GL_LINES);
            for(int i=ui->U+delta;i<ui->B;i+=delta*2){ tnsVertex2d(ui->L,i); tnsVertex2d(ui->R,i); } tnsColor4d(0,0,0,0.5); tnsPackAs(GL_LINES);
            for(int i=ui->U+delta*2;i<ui->B;i+=delta*2){ tnsVertex2d(ui->L,i); tnsVertex2d(ui->R,i); } tnsColor4d(1,1,1,0.5); tnsPackAs(GL_LINES);
        }
        char buf[128]; sprintf(buf,"%.1lf%%",100.0f/e->ZoomX);
        tnsDrawStringAuto(buf,colork,ui->L+LA_M+1,ui->R-LA_M,ui->B-LA_RH-LA_M+1,0);
        tnsDrawStringAuto(buf,colorw,ui->L+LA_M,ui->R-LA_M,ui->B-LA_RH-LA_M,0);
    }
    
    la_CanvasDefaultOverlay(ui, h);
}

void our_GetBrushOffset(OurCanvasDraw* ocd_if_scale, OurBrush*b, real event_orientation, real*x, real*y){
    *x=*y=0;
    real offx=0,offy=0;
    if(b && b->VisualOffset>1e-4){ real offset=b->VisualOffset;
        real orientation = b->OffsetFollowPenTilt?event_orientation:b->VisualOffsetAngle;
        real zoom=ocd_if_scale?ocd_if_scale->Base.ZoomX:1;
        offx = cos(orientation)*zoom*LA_RH*offset; offy = sin(orientation)*zoom*LA_RH*offset * (ocd_if_scale?-1:1);
    }
    *x=offx; *y=offy;
}

int ourextramod_Canvas(laOperator *a, laEvent *e){
    laUiItem *ui = a->Instance; OurCanvasDraw* ocd=ui->Extra;
    if(ocd->Base.SelectThrough && e->type==LA_L_MOUSE_DOWN) return LA_RUNNING;
    if(Our->EnableBrushCircle && ((e->type&LA_MOUSE_EVENT)||(e->type&LA_KEYBOARD_EVENT))){
        ocd->PointerX = e->x; ocd->PointerY = e->y; real offx,offy;
        our_GetBrushOffset(0,Our->CurrentBrush,e->Orientation,&offx,&offy);
        ocd->Base.OnX=e->x-offx; ocd->Base.OnY=e->y-offy;
        laRedrawCurrentPanel(); Our->EventHasTwist=e->HasTwist; Our->EventTwistAngle=e->Twist;
        Our->EventTiltOrientation=e->Orientation;
    }
    return LA_RUNNING_PASS;
}

OurLayer* our_NewLayer(char* name){
    OurLayer* l=memAcquire(sizeof(OurLayer)); strSafeSet(&l->Name,name); lstPushItem(&Our->Layers, l);
    memAssignRef(Our, &Our->CurrentLayer, l);
    return l;
}
void our_DuplicateLayerContent(OurLayer* to, OurLayer* from){
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!from->TexTiles[row]) continue;
        to->TexTiles[row]=memAcquire(sizeof(OurTexTile*)*OUR_TILES_PER_ROW);
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!from->TexTiles[row][col] || !from->TexTiles[row][col]->Texture) continue;
            to->TexTiles[row][col]=memAcquire(sizeof(OurTexTile));
            OurTexTile* tt=to->TexTiles[row][col],*ft=from->TexTiles[row][col];
            memcpy(tt,ft,sizeof(OurTexTile));
            tt->CopyBuffer=0;
            tt->Texture=tnsCreate2DTexture(OUR_CANVAS_GL_PIX,OUR_TILE_W,OUR_TILE_W,0);
            int bufsize=OUR_TILE_W*OUR_TILE_W*OUR_CANVAS_PIXEL_SIZE;
            tt->FullData=malloc(bufsize);

            ft->Data=malloc(bufsize); int width=OUR_TILE_W;
            tnsBindTexture(ft->Texture); glPixelStorei(GL_PACK_ALIGNMENT, 1);
            tnsGet2DTextureSubImage(ft->Texture, 0, 0, width, width, OUR_CANVAS_GL_FORMAT, OUR_CANVAS_DATA_FORMAT, bufsize, ft->Data);
            tnsBindTexture(tt->Texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, width, OUR_CANVAS_GL_FORMAT, OUR_CANVAS_DATA_FORMAT, ft->Data);
            
            free(ft->Data); ft->Data=0;
        }
    }
}
void ourbeforefree_Layer(OurLayer* l){
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!l->TexTiles[row]) continue;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!l->TexTiles[row][col]) continue;
            if(l->TexTiles[row][col]->Texture){ tnsDeleteTexture(l->TexTiles[row][col]->Texture); l->TexTiles[row][col]->Texture=0; }
            if(l->TexTiles[row][col]->Data){ free(l->TexTiles[row][col]->Data); l->TexTiles[row][col]->Data=0; }
            if(l->TexTiles[row][col]->FullData){ free(l->TexTiles[row][col]->FullData); l->TexTiles[row][col]->FullData=0; }
            if(l->TexTiles[row][col]->CopyBuffer){ free(l->TexTiles[row][col]->CopyBuffer); l->TexTiles[row][col]->CopyBuffer=0; }
            memFree(l->TexTiles[row][col]);
        }
        memFree(l->TexTiles[row]); l->TexTiles[row]=0;
    }
}
void our_RemoveLayer(OurLayer* l, int cleanup){
    strSafeDestroy(&l->Name);
    if(Our->CurrentLayer==l){ OurLayer* nl=l->Item.pPrev?l->Item.pPrev:l->Item.pNext; memAssignRef(Our, &Our->CurrentLayer, nl); }
    lstRemoveItem(&Our->Layers, l);
    if(cleanup) ourbeforefree_Layer(l);
    memLeave(l);
}
int our_MergeLayer(OurLayer* l){
    OurLayer* ol=l->Item.pNext; if(!ol) return 0; int xmin=INT_MAX,xmax=-INT_MAX,ymin=INT_MAX,ymax=-INT_MAX; int seam=OUR_TILE_SEAM;
    glUseProgram(Our->CompositionProgram);
    glUniform1i(Our->uBlendMode, l->BlendMode);
    glUniform1f(Our->uAlphaTop, 1-l->Transparency);
    glUniform1f(Our->uAlphaBottom, 1-ol->Transparency);
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!l->TexTiles[row]) continue;// Should not happen.
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!l->TexTiles[row][col]) continue; OurTexTile*t=l->TexTiles[row][col];
            if(!t->Texture) continue;
            int tl,tr,tu,tb; our_LayerEnsureTileDirect(ol,row,col);
            OurTexTile*ot=ol->TexTiles[row][col];
            if((!ot) || (!ot->Texture)) our_LayerEnsureTileDirect(ol,row,col);
            glBindImageTexture(0, t->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, OUR_CANVAS_GL_PIX);
            glBindImageTexture(1, ot->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, OUR_CANVAS_GL_PIX);
            glDispatchCompute(OUR_WORKGROUP_SIZE,OUR_WORKGROUP_SIZE,1);
            xmin=TNS_MIN2(xmin,t->l+seam);xmax=TNS_MAX2(xmax,t->r-seam); ymin=TNS_MIN2(ymin,t->b+seam);ymax=TNS_MAX2(ymax,t->u-seam);
        }
    }
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    if(xmin>xmax||ymin>ymax) return 0;

    our_RecordUndo(l,xmin,xmax,ymin,ymax,1,0);
    our_RecordUndo(ol,xmin,xmax,ymin,ymax,1,0);
    our_RemoveLayer(l,0);
    laRecordDifferences(0,"our.canvas.layers");laRecordDifferences(0,"our.canvas.current_layer");
    laPushDifferences("Merge layers",0);

    return 1;
}

OurBrush* our_NewBrush(char* name, real SizeOffset, real Hardness, real DabsPerSize, real Transparency, real Smudge, real SmudgeResampleLength,
    int PressureSize, int PressureHardness, int PressureTransparency, int PressureSmudge){
    OurBrush* b=memAcquireHyper(sizeof(OurBrush)); strSafeSet(&b->Name,name); lstAppendItem(&Our->Brushes, b);
    b->SizeOffset=SizeOffset; b->Hardness=Hardness; b->DabsPerSize=DabsPerSize; b->Transparency=Transparency; b->Smudge=Smudge;
    b->PressureHardness=PressureHardness; b->PressureSize=PressureSize; b->PressureTransparency=PressureTransparency; b->PressureSmudge=PressureSmudge;
    b->SmudgeResampleLength = SmudgeResampleLength;
    memAssignRef(Our, &Our->CurrentBrush, b);
    b->Rack=memAcquire(sizeof(laRackPage)); b->Rack->RackType=LA_RACK_TYPE_DRIVER;
    b->Binding=-1;
    b->PressureForce=1; b->Force=1; b->VisualOffsetAngle=TNS_PI/4;
    return b;
}
void our_RemoveBrush(OurBrush* b){
    strSafeDestroy(&b->Name); strSafeDestroy(&b->Custom1Name); strSafeDestroy(&b->Custom2Name);
    if(Our->CurrentBrush==b){ OurBrush* nb=b->Item.pPrev?b->Item.pPrev:b->Item.pNext; memAssignRef(Our, &Our->CurrentBrush, nb); }
    lstRemoveItem(&Our->Brushes, b);
    memLeave(b->Rack); b->Rack=0;
    memLeave(b);
}
OurBrush* our_DuplicateBrush(OurBrush* b){
    OurBrush* nb=memAcquireHyper(sizeof(OurBrush));
    memcpy(nb,b,sizeof(OurBrush)); nb->Binding=-1; nb->Name=0; strSafePrint(&nb->Name,"%s Copy",b->Name?b->Name->Ptr:"New Brush");
    nb->Rack=laDuplicateRackPage(0,b->Rack);
    nb->Item.pNext=nb->Item.pPrev=0;
    lstInsertItemAfter(&Our->Brushes,nb,b);
    memAssignRef(Our, &Our->CurrentBrush, nb);
    return nb;
}

int our_BufferAnythingVisible(OUR_PIX_COMPACT* buf, int elemcount){
    for(int i=0;i<elemcount;i++){
        OUR_PIX_COMPACT* rgba=&buf[i*4]; if(rgba[3]) return 1;
    }
    return 0;
}
void our_TileEnsureUndoBuffer(OurTexTile* t, real xmin,real xmax, real ymin,real ymax,int OnlyUpdateLocal){
    if(!t->Texture) return;
    if(OnlyUpdateLocal){
        t->cl=0;t->cr=OUR_TILE_W;t->cu=OUR_TILE_W;t->cb=0;
    }else{
        int _l=floor(xmin),_r=ceil(xmax),_u=ceil(ymax),_b=floor(ymin);
        if(t->CopyBuffer){ free(t->CopyBuffer); t->CopyBuffer=0; } //shouldn't happen
        if(_l>=t->r || _r<t->l || _b>=t->u || _u<t->b || _l==_r || _u==_b) return;
        t->cl=TNS_MAX2(_l,t->l)-t->l;t->cr=TNS_MIN2(_r,t->r)-t->l;t->cu=TNS_MIN2(_u,t->u)-t->b;t->cb=TNS_MAX2(_b,t->b)-t->b;
    }
    int rows=t->cu-t->cb,cols=t->cr-t->cl;
    int bufsize=cols*rows*OUR_CANVAS_PIXEL_SIZE;
    t->CopyBuffer=calloc(1,bufsize);
    for(int row=0;row<rows;row++){
        memcpy(&t->CopyBuffer[row*cols*4],&t->FullData[((+row+t->cb)*OUR_TILE_W+t->cl)*4],OUR_CANVAS_PIXEL_SIZE*cols);
    }
    OUR_PIX_COMPACT* temp=malloc(bufsize);
    tnsBindTexture(t->Texture);
    tnsGet2DTextureSubImage(t->Texture, t->cl, t->cb, cols,rows, OUR_CANVAS_GL_FORMAT, OUR_CANVAS_DATA_FORMAT, bufsize, temp);
    for(int row=0;row<rows;row++){
        memcpy(&t->FullData[((+row+t->cb)*OUR_TILE_W+t->cl)*4],&temp[row*cols*4],sizeof(OUR_PIX_COMPACT)*4*cols);
    }
    free(temp);
}
void our_TileSwapBuffers(OurTexTile* t, OUR_PIX_COMPACT* data, int IsRedo, int l, int r, int u, int b){
    int rows=u-b,cols=r-l; int bufsize=rows*cols*OUR_CANVAS_PIXEL_SIZE;
    OUR_PIX_COMPACT* temp=malloc(bufsize);
    memcpy(temp,data,bufsize);
    for(int row=0;row<rows;row++){
        memcpy(&data[row*cols*4],&t->FullData[((+row+b)*OUR_TILE_W+l)*4],OUR_CANVAS_PIXEL_SIZE*cols);
        memcpy(&t->FullData[((+row+b)*OUR_TILE_W+l)*4],&temp[row*cols*4],OUR_CANVAS_PIXEL_SIZE*cols);
    }
    tnsBindTexture(t->Texture);
    glGetError();
    OUR_PIX_COMPACT* use_data=temp;
    if(IsRedo){ use_data=data; }
    glTexSubImage2D(GL_TEXTURE_2D, 0, l, b, cols,rows,OUR_CANVAS_GL_FORMAT,OUR_CANVAS_DATA_FORMAT,use_data);
    free(temp);
}
void ourundo_Tiles(OurUndo* undo){
    for(OurUndoTile* ut=undo->Tiles.pFirst;ut;ut=ut->Item.pNext){
        our_LayerEnsureTileDirect(undo->Layer,ut->row,ut->col);
        our_TileSwapBuffers(undo->Layer->TexTiles[ut->row][ut->col], ut->CopyData, 0, ut->l, ut->r, ut->u, ut->b);
    }
    laNotifyUsers("our.canvas_notify");
}
void ourredo_Tiles(OurUndo* undo){
    for(OurUndoTile* ut=undo->Tiles.pFirst;ut;ut=ut->Item.pNext){
        our_LayerEnsureTileDirect(undo->Layer,ut->row,ut->col);
        our_TileSwapBuffers(undo->Layer->TexTiles[ut->row][ut->col], ut->CopyData, 0, ut->l, ut->r, ut->u, ut->b);
    }
    laNotifyUsers("our.canvas_notify");
}
void ourundo_Free(OurUndo* undo,int FromLeft){
    OurUndoTile* ut;
    while(ut=lstPopItem(&undo->Tiles)){ free(ut->CopyData); memFree(ut); }
    memFree(undo);
}
#define OUR_XXYY_TO_COL_ROW_RANGE \
    l=(int)(floor(OUR_TILE_CTR+(xmin-OUR_TILE_SEAM)/OUR_TILE_W_USE+0.5));\
    r=(int)(floor(OUR_TILE_CTR+(xmax+OUR_TILE_SEAM)/OUR_TILE_W_USE+0.5));\
    u=(int)(floor(OUR_TILE_CTR+(ymax+OUR_TILE_SEAM)/OUR_TILE_W_USE+0.5));\
    b=(int)(floor(OUR_TILE_CTR+(ymin-OUR_TILE_SEAM)/OUR_TILE_W_USE+0.5));\
    TNS_CLAMP(l,0,OUR_TILES_PER_ROW-1); TNS_CLAMP(r,0,OUR_TILES_PER_ROW-1); TNS_CLAMP(u,0,OUR_TILES_PER_ROW-1); TNS_CLAMP(b,0,OUR_TILES_PER_ROW-1);
#define OUR_XXYY_TO_COL_ROW_ALIGNED \
    l=(int)(floor(OUR_TILE_CTR+(xmin)/OUR_TILE_W_USE+0.5));\
    r=(int)(floor(OUR_TILE_CTR+(xmax-1)/OUR_TILE_W_USE+0.5));\
    u=(int)(floor(OUR_TILE_CTR+(ymax-1)/OUR_TILE_W_USE+0.5));\
    b=(int)(floor(OUR_TILE_CTR+(ymin)/OUR_TILE_W_USE+0.5));\
    TNS_CLAMP(l,0,OUR_TILES_PER_ROW-1); TNS_CLAMP(r,0,OUR_TILES_PER_ROW-1); TNS_CLAMP(u,0,OUR_TILES_PER_ROW-1); TNS_CLAMP(b,0,OUR_TILES_PER_ROW-1);
void our_RecordUndo(OurLayer* ol, real xmin,real xmax, real ymin,real ymax,int Aligned,int Push){
    if(xmax<xmin || ymax<ymin) return;
    int l,r,u,b;
    if(Aligned){ OUR_XXYY_TO_COL_ROW_ALIGNED }else{ OUR_XXYY_TO_COL_ROW_RANGE; }
    OurUndo* undo=memAcquire(sizeof(OurUndo)); undo->Layer=ol;
    for(int row=b;row<=u;row++){ if(!ol->TexTiles[row]) continue;// Should not happen.
        for(int col=l;col<=r;col++){ if(!ol->TexTiles[row][col]) continue; OurTexTile*t=ol->TexTiles[row][col];
            our_TileEnsureUndoBuffer(t,xmin,xmax,ymin,ymax,0);
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
    if(Push){ laPushDifferences("Paint",0); laFreeOlderDifferences(Our->PaintUndoLimit); }
}
void our_LayerRefreshLocal(OurLayer* ol){
    //OurUndo* undo=memAcquire(sizeof(OurUndo)); undo->Layer=ol;
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue; OurTexTile*t=ol->TexTiles[row][col];
            our_TileEnsureUndoBuffer(t,0,0,0,0,1);
        }
    }
    //if(!undo->Tiles.pFirst){ memFree(undo); return; /*unlikely;*/ }
    //laFreeNewerDifferences();
    //laRecordCustomDifferences(undo,ourundo_Tiles,ourredo_Tiles,ourundo_Free);
    //if(Push){ laPushDifferences("Loaded",0); }
}
void our_LayerEnsureTileDirect(OurLayer* ol, int row, int col){
    if(!ol->TexTiles[row]){ol->TexTiles[row]=memAcquireSimple(sizeof(OurTexTile*)*OUR_TILES_PER_ROW);}
    if(!ol->TexTiles[row][col]) ol->TexTiles[row][col]=memAcquireSimple(sizeof(OurTexTile));
    OurTexTile*t=ol->TexTiles[row][col];
    if(t->Texture) return;
    t->Texture=tnsCreate2DTexture(OUR_CANVAS_GL_PIX,OUR_TILE_W,OUR_TILE_W,0);
    int sx=((real)col-OUR_TILE_CTR-0.5)*OUR_TILE_W_USE,sy=((real)row-OUR_TILE_CTR-0.5)*OUR_TILE_W_USE;
    t->l=sx-OUR_TILE_SEAM,t->b=sy-OUR_TILE_SEAM; t->r=t->l+OUR_TILE_W; t->u=t->b+OUR_TILE_W;
    uint16_t initColor[]={0,0,0,0};
    tnsClearTextureImage(t->Texture,OUR_CANVAS_GL_FORMAT,OUR_CANVAS_DATA_FORMAT);
    t->FullData=calloc(OUR_TILE_W,OUR_TILE_W*OUR_CANVAS_PIXEL_SIZE);
}
void our_LayerEnsureTiles(OurLayer* ol, real xmin,real xmax, real ymin,real ymax, int Aligned, int *tl, int *tr, int* tu, int* tb){
    int l,r,u,b;
    if(Aligned){ OUR_XXYY_TO_COL_ROW_ALIGNED }else{ OUR_XXYY_TO_COL_ROW_RANGE; }
    for(int row=b;row<=u;row++){
        for(int col=l;col<=r;col++){
            our_LayerEnsureTileDirect(ol,row,col);
        }
    }
    *tl=l; *tr=r; *tu=u; *tb=b;
}
void our_TileTextureToImage(OurTexTile* ot, int SX, int SY, int composite, int BlendMode, real alpha){
    if(!ot->Texture) return;
    int bufsize=OUR_TILE_W_USE*OUR_TILE_W_USE*OUR_CANVAS_PIXEL_SIZE;
    ot->Data=malloc(bufsize); int seam=OUR_TILE_SEAM; int width=OUR_TILE_W_USE;
    tnsBindTexture(ot->Texture); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    tnsGet2DTextureSubImage(ot->Texture, seam, seam, width, width, OUR_CANVAS_GL_FORMAT, OUR_CANVAS_DATA_FORMAT, bufsize, ot->Data);
    OUR_PIX_COMPACT* image_buffer=Our->ImageBuffer;
    if(composite){
        for(int row=0;row<OUR_TILE_W_USE;row++){
            for(int col=0;col<OUR_TILE_W_USE;col++){
                if(BlendMode==OUR_BLEND_NORMAL){
                    our_CanvasAlphaMix(&image_buffer[((int64_t)(SY+row)*Our->ImageW+SX+col)*4], &ot->Data[(row*OUR_TILE_W_USE+col)*4],alpha);
                }elif(BlendMode==OUR_BLEND_ADD){
                    our_CanvasAdd(&image_buffer[((int64_t)(SY+row)*Our->ImageW+SX+col)*4], &ot->Data[(row*OUR_TILE_W_USE+col)*4],alpha);
                }
            }
        }
    }else{
        for(int row=0;row<OUR_TILE_W_USE;row++){
            memcpy(&image_buffer[((int64_t)(SY+row)*Our->ImageW+SX)*4],&ot->Data[(row*OUR_TILE_W_USE)*4],OUR_CANVAS_PIXEL_SIZE*OUR_TILE_W_USE);
        }
    }
    free(ot->Data); ot->Data=0;
}
void our_TileImageToTexture(OurTexTile* ot, int SX, int SY){
    if(!ot->Texture) return;
    int pl=(SX!=0)?OUR_TILE_SEAM:0, pr=((SX+OUR_TILE_W_USE)!=Our->ImageW)?OUR_TILE_SEAM:0;
    int pu=(SY!=0)?OUR_TILE_SEAM:0, pb=((SY+OUR_TILE_W_USE)!=Our->ImageH)?OUR_TILE_SEAM:0;
    int bufsize=(OUR_TILE_W+pl+pr)*(OUR_TILE_W+pu+pb)*OUR_CANVAS_PIXEL_SIZE;
    ot->Data=malloc(bufsize); int width=OUR_TILE_W_USE+pl+pr, height=OUR_TILE_W_USE+pu+pb;
    OUR_PIX_COMPACT* image_buffer = Our->ImageBuffer;
    for(int row=0;row<height;row++){
        memcpy(&ot->Data[((row)*width)*4],&image_buffer[((int64_t)(SY+row-pu)*Our->ImageW+SX-pl)*4],OUR_CANVAS_PIXEL_SIZE*width);
    }
    if(!our_BufferAnythingVisible(ot->Data, bufsize/OUR_CANVAS_PIXEL_SIZE)){ tnsDeleteTexture(ot->Texture); ot->Texture=0; }
    else{
        tnsBindTexture(ot->Texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, OUR_TILE_SEAM-pl, OUR_TILE_SEAM-pu, width, height, OUR_CANVAS_GL_FORMAT, OUR_CANVAS_DATA_FORMAT, ot->Data);
    }
    free(ot->Data); ot->Data=0;
}
int our_LayerEnsureImageBuffer(OurLayer* ol, int OnlyCalculate){
    int l=1000,r=-1000,u=-1000,b=1000; int any=0;
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        if(row<b) b=row; if(row>u) u=row;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col] || !ol->TexTiles[row][col]->Texture) continue;
            if(col<l) l=col; if(col>r) r=col; any++;
        }
    }
    if(!any) return -1;
    Our->ImageW = OUR_TILE_W_USE*(r-l+1); Our->ImageH = OUR_TILE_W_USE*(u-b+1);
    Our->ImageX =((real)l-OUR_TILE_CTR-0.5)*OUR_TILE_W_USE; Our->ImageY=((real)b-OUR_TILE_CTR-0.5)*OUR_TILE_W_USE;
    if(!OnlyCalculate){
        if(Our->ImageBuffer) free(Our->ImageBuffer);
        Our->ImageBuffer = calloc(Our->ImageW*4,Our->ImageH*sizeof(uint16_t));
        if(!Our->ImageBuffer){ return 0; }
    }
    return 1;
}
void our_LayerClearEmptyTiles(OurLayer* ol);
int our_CanvasEnsureImageBuffer(){
    int x=INT_MAX,y=INT_MAX,w=-INT_MAX,h=-INT_MAX;
    for(OurLayer* l=Our->Layers.pFirst;l;l=l->Item.pNext){
        our_LayerClearEmptyTiles(l);
        our_LayerEnsureImageBuffer(l,1);
        if(Our->ImageX<x) x=Our->ImageX; if(Our->ImageY<y) y=Our->ImageY;
        if(Our->ImageW>w) w=Our->ImageW; if(Our->ImageH>h) h=Our->ImageH;
    }
    if(w<0||h<0) return 0;
    Our->ImageX=x; Our->ImageY=y; Our->ImageW=w; Our->ImageH=h;
    if(Our->ImageBuffer) free(Our->ImageBuffer);
    Our->ImageBuffer = calloc(Our->ImageW*4,Our->ImageH*sizeof(uint16_t));
    if(!Our->ImageBuffer){ return 0; }
    return 1;
}
void our_CanvasFillImageBufferBackground(int transparent){
    if(transparent){ return; } // it should already be 0,0,0,0.
    int64_t count=Our->ImageW*Our->ImageH;
    real bk[4]; tnsVectorSet3v(bk,Our->BackgroundColor); bk[3]=1;
    Our->BColorU16[0]=bk[0]*65535; Our->BColorU16[1]=bk[1]*65535; Our->BColorU16[2]=bk[2]*65535; Our->BColorU16[3]=65535;
    Our->BColorU8[0]=0.5+bk[0]*255; Our->BColorU8[1]=0.5+bk[1]*255; Our->BColorU8[2]=0.5+bk[2]*255; Our->BColorU8[3]=255;
    OUR_PIX_COMPACT* image_buffer = Our->ImageBuffer;
    for(int64_t i=0;i<count;i++){
        OUR_PIX_COMPACT* p=&image_buffer[(int64_t)i*4];
#ifdef LA_USE_GLES
        tnsVectorSet4v(p,Our->BColorU8);
#else
        tnsVectorSet4v(p,Our->BColorU16);
#endif
    }
}
void our_ImageBufferFromNative(){
#ifdef LA_USE_GLES
    int pixcount = Our->ImageH*Our->ImageW;
    uint16_t* converted_buffer = malloc(pixcount * sizeof(uint16_t)*4);
    uint8_t* image_buffer = Our->ImageBuffer;
    for(int i=0;i<pixcount;i++){
        converted_buffer[i*4] = ((uint16_t)image_buffer[i*4]) << 8;
        converted_buffer[i*4 + 1] = ((uint16_t)image_buffer[i*4 + 1]) << 8;
        converted_buffer[i*4 + 2] = ((uint16_t)image_buffer[i*4 + 2]) << 8;
        converted_buffer[i*4 + 3] = ((uint16_t)image_buffer[i*4 + 3]) << 8;
    }
    free(Our->ImageBuffer); Our->ImageBuffer = converted_buffer;
#endif
}
void our_ImageBufferToNative(){
#ifdef LA_USE_GLES
    int pixcount = Our->ImageH*Our->ImageW;
    uint8_t* converted_buffer = malloc(pixcount * sizeof(uint8_t)*4);
    uint16_t* image_buffer = Our->ImageBuffer;
    for(int i=0;i<pixcount;i++){
        converted_buffer[i*4] = image_buffer[i*4] >> 8;
        converted_buffer[i*4 + 1] = image_buffer[i*4 + 1] >> 8;
        converted_buffer[i*4 + 2] = image_buffer[i*4 + 2] >> 8;
        converted_buffer[i*4 + 3] = image_buffer[i*4 + 3] >> 8;
    }
    free(Our->ImageBuffer); Our->ImageBuffer = converted_buffer;
#endif
}
void our_LayerToImageBuffer(OurLayer* ol, int composite){
    if(composite && (ol->Hide || ol->Transparency==1 || (Our->SketchMode==2 && ol->AsSketch))) return;
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue;
            int sx=ol->TexTiles[row][col]->l+OUR_TILE_SEAM,sy=ol->TexTiles[row][col]->b+OUR_TILE_SEAM;
            our_TileTextureToImage(ol->TexTiles[row][col], sx-Our->ImageX, sy-Our->ImageY, composite, ol->BlendMode, 1.0f-ol->Transparency);
        }
    }
}
void our_LayerToTexture(OurLayer* ol){
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue;
            int sx=ol->TexTiles[row][col]->l+OUR_TILE_SEAM,sy=ol->TexTiles[row][col]->b+OUR_TILE_SEAM;
            our_TileImageToTexture(ol->TexTiles[row][col], sx-Our->ImageX, sy-Our->ImageY);
        }
    }
}
void our_GetFinalDimension(int UseFrame, int SegmentY,int SegmentH, int* x, int* y, int* w, int* h){
    if(UseFrame){ *x=Our->X; *y=Our->Y; *w=Our->W; *h=Our->H; }
    else{ *x=Our->ImageX; *y=Our->ImageY; *w=Our->ImageW; *h=Our->ImageH; }
    if(SegmentH>0){ *y=SegmentY; *h=SegmentH; }
    //printf("%d %d %d %d, %d %d %d %d\n",Our->X, Our->Y, Our->W, Our->H,Our->ImageX, Our->ImageY, Our->ImageW, Our->ImageH);
}
#define GET_FINAL_ROW_TYPE(TYPE,BCOLOR) \
TYPE* our_GetFinalRow_##TYPE(int UseFrame, int row, int x, int y, int w, int h, TYPE* temp){\
    if(!UseFrame) return &((TYPE*)Our->ImageBuffer)[(int64_t)Our->ImageW*(Our->ImageH-row-1)*4];\
    int userow=(h-row-1)-(Our->ImageY-(y-h));\
    if(userow<0 || userow>=Our->ImageH){ for(int i=0;i<w;i++){ tnsVectorSet4v(&temp[(int64_t)i*4],BCOLOR); } return temp; }\
    int sstart=x>Our->ImageX?(x-Our->ImageX):0, tstart=x>Our->ImageX?0:(Our->ImageX-x);\
    int slen=(x+w>Our->ImageX+Our->ImageW)?(Our->ImageW-sstart):(Our->ImageW-sstart-(Our->ImageX+Our->ImageW-x-w));\
    for(int i=0;i<tstart;i++){ tnsVectorSet4v(&temp[(int64_t)i*4],BCOLOR); }\
    for(int i=sstart+slen;i<w;i++){ tnsVectorSet4v(&temp[(int64_t)i*4],BCOLOR); }\
    memcpy(&temp[(int64_t)tstart*4],&((TYPE*)Our->ImageBuffer)[(int64_t)(Our->ImageW*(userow)+sstart)*4],slen*sizeof(TYPE)*4);\
    return temp;\
}
GET_FINAL_ROW_TYPE(uint16_t,Our->BColorU16)
GET_FINAL_ROW_TYPE(uint8_t,Our->BColorU8)
typedef void* (*ourGetFinalRowFunc)(int UseFrame, int row, int x, int y, int w, int h, void* temp);
static void _our_png_write(png_structp png_ptr, png_bytep data, png_size_t length){
    OurLayerWrite* LayerWrite=png_get_io_ptr(png_ptr);
    arrEnsureLength(&LayerWrite->data,LayerWrite->NextData+length,&LayerWrite->MaxData,sizeof(unsigned char));
    memcpy(&LayerWrite->data[LayerWrite->NextData], data, length);
    LayerWrite->NextData+=length;
}
void our_ImageConvertForExport(int BitDepth, int ColorProfile){
    uint8_t* NewImage;
    cmsHTRANSFORM cmsTransform = NULL;
    cmsHPROFILE input_buffer_profile=NULL,input_gamma_profile=NULL;
    cmsHPROFILE output_buffer_profile=NULL;

    /* unpremultiply */
    uint16_t* image_buffer=Our->ImageBuffer;
    for(int row=0;row<Our->ImageH;row++){
        for(int col=0;col<Our->ImageW;col++){ uint16_t* p=&image_buffer[((int64_t)row*Our->ImageW+col)*4];
            uint16_t a=(real)p[3]/65535.0f;
            if(a>0){
                p[0]=(p[0]<p[3])?p[0]:((real)p[0]/a);
                p[1]=(p[1]<p[3])?p[1]:((real)p[1]/a);
                p[2]=(p[2]<p[3])?p[2]:((real)p[2]/a);
            }
        }
    }

    if(BitDepth==OUR_EXPORT_BIT_DEPTH_16){ return; /* only export 16bit flat */ }

    input_buffer_profile=(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_CLAY)?
        cmsOpenProfileFromMem(Our->icc_LinearClay,Our->iccsize_LinearClay):
        ((Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_D65_P3)?
            cmsOpenProfileFromMem(Our->icc_LinearD65P3,Our->iccsize_LinearD65P3):
            cmsOpenProfileFromMem(Our->icc_LinearsRGB,Our->iccsize_LinearsRGB));
    input_gamma_profile=(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_CLAY)?
        cmsOpenProfileFromMem(Our->icc_Clay,Our->icc_Clay):
        ((Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_D65_P3)?
            cmsOpenProfileFromMem(Our->icc_D65P3,Our->iccsize_D65P3):
            cmsOpenProfileFromMem(Our->icc_sRGB,Our->iccsize_sRGB));

    NewImage=calloc(Our->ImageW*sizeof(uint8_t),Our->ImageH*4);
    if(NewImage){
        int64_t total_pixels = (int64_t)Our->ImageW*Our->ImageH;
        if(ColorProfile!=OUR_EXPORT_COLOR_MODE_FLAT && total_pixels<=UINT32_MAX){
            if(ColorProfile==OUR_EXPORT_COLOR_MODE_SRGB){ output_buffer_profile=cmsOpenProfileFromMem(Our->icc_sRGB,Our->iccsize_sRGB); }
            elif(ColorProfile==OUR_EXPORT_COLOR_MODE_CLAY){ output_buffer_profile=cmsOpenProfileFromMem(Our->icc_Clay,Our->iccsize_Clay); }
            cmsTransform = cmsCreateTransform(input_buffer_profile, TYPE_RGBA_16, input_gamma_profile, TYPE_RGBA_8,
                INTENT_ABSOLUTE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA|cmsFLAGS_HIGHRESPRECALC);
            cmsDoTransform(cmsTransform,Our->ImageBuffer,NewImage,total_pixels);
            cmsDeleteTransform(cmsTransform);
            if(input_gamma_profile!=output_buffer_profile){
                cmsTransform = cmsCreateTransform(input_gamma_profile, TYPE_RGBA_8, output_buffer_profile, TYPE_RGBA_8,
                    INTENT_ABSOLUTE_COLORIMETRIC, cmsFLAGS_COPY_ALPHA|cmsFLAGS_HIGHRESPRECALC);
                cmsDoTransform(cmsTransform,NewImage,NewImage,total_pixels);
                cmsDeleteTransform(cmsTransform);
            }
        }else{
            if(total_pixels>UINT32_MAX){
                logPrintNew("Export: [TODO] Image pixel count exceeds UINT32_MAX, not doing any transforms.\n");
            }
            for(int row=0;row<Our->ImageH;row++){
                for(int col=0;col<Our->ImageW;col++){ uint8_t* p=&NewImage[((int64_t)row*Our->ImageW+col)*4]; uint16_t* p0=&Our->ImageBuffer[((int64_t)row*Our->ImageW+col)*4];
                    p[0]=((real)p0[0])/256; p[1]=((real)p0[1])/256; p[2]=((real)p0[2])/256; p[3]=((real)p0[3])/256;
                }
            }
        }
    }
    cmsCloseProfile(input_buffer_profile);cmsCloseProfile(input_gamma_profile);cmsCloseProfile(output_buffer_profile);
    free(Our->ImageBuffer); Our->ImageBuffer=NewImage;
}
int our_ImageExportPNG(FILE* fp, int WriteToBuffer, void** buf, int* sizeof_buf, int UseFrame, int BitDepth, int ColorProfile, int SegmentY, int SegmentH){
    if((!fp)&&(!WriteToBuffer)) return 0;
    if(!Our->ImageBuffer) return 0;
    real bk[4]; tnsVectorSet3v(bk,Our->BackgroundColor); bk[3]=1;

    int UseBitDepth,ElemSize; void* use_icc=0; int use_icc_size;
    ourGetFinalRowFunc GetFinalRow;

    if(BitDepth==OUR_EXPORT_BIT_DEPTH_16){ UseBitDepth=16; ElemSize=sizeof(uint16_t); ColorProfile=OUR_EXPORT_COLOR_MODE_FLAT; GetFinalRow=our_GetFinalRow_uint16_t; }
    else{ UseBitDepth=8; ElemSize=sizeof(uint8_t); GetFinalRow=our_GetFinalRow_uint8_t; }

    png_structp png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING,0,0,0);
    png_infop info_ptr = png_create_info_struct(png_ptr);

    OurLayerWrite LayerWrite={0};

    if(WriteToBuffer){
        arrEnsureLength(&LayerWrite.data,0,&LayerWrite.MaxData,sizeof(unsigned char));
        png_set_write_fn(png_ptr,&LayerWrite,_our_png_write,0);
    }else{
        png_init_io(png_ptr, fp);
    }

    int X,Y,W,H; our_GetFinalDimension(UseFrame,SegmentY,SegmentH, &X,&Y,&W,&H);
    
    png_set_IHDR(png_ptr, info_ptr,W,H,UseBitDepth,PNG_COLOR_TYPE_RGBA,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_BASE,PNG_FILTER_TYPE_BASE);
    if(ColorProfile==OUR_EXPORT_COLOR_MODE_SRGB){ png_set_sRGB(png_ptr,info_ptr,PNG_sRGB_INTENT_PERCEPTUAL);use_icc=Our->icc_sRGB;use_icc_size=Our->iccsize_sRGB;tns2LogsRGB(bk); }
    elif(ColorProfile==OUR_EXPORT_COLOR_MODE_CLAY){ use_icc=Our->icc_Clay;use_icc_size=Our->iccsize_Clay;tns2LogsRGB(bk);/* should be clay */ }
    elif(ColorProfile==OUR_EXPORT_COLOR_MODE_D65_P3){ use_icc=Our->icc_D65P3;use_icc_size=Our->iccsize_D65P3;tns2LogsRGB(bk);/* should be clay */ }
    elif(ColorProfile==OUR_EXPORT_COLOR_MODE_FLAT){ 
        if(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_SRGB){use_icc=Our->icc_LinearsRGB;use_icc_size=Our->iccsize_LinearsRGB;}
        elif(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_CLAY){use_icc=Our->icc_LinearClay;use_icc_size=Our->iccsize_LinearClay;}
        elif(Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_D65_P3){use_icc=Our->icc_LinearD65P3;use_icc_size=Our->iccsize_LinearD65P3;}
    }
    if(use_icc){ png_set_iCCP(png_ptr,info_ptr,"LA_PROFILE",PNG_COMPRESSION_TYPE_BASE,use_icc,use_icc_size); }

    png_write_info(png_ptr, info_ptr);
    png_set_swap(png_ptr);

    Our->BColorU16[0]=bk[0]*65535; Our->BColorU16[1]=bk[1]*65535; Our->BColorU16[2]=bk[2]*65535; Our->BColorU16[3]=65535;
    Our->BColorU8[0]=0.5+bk[0]*255; Our->BColorU8[1]=0.5+bk[1]*255; Our->BColorU8[2]=0.5+bk[2]*255; Our->BColorU8[3]=255;

    char* temp_row=calloc(W,ElemSize*4);

    int prog=0,lastprog=0;
    for(int i=0;i<H;i++){
        char* final=GetFinalRow(UseFrame,i+SegmentY,X,Y,W,H,temp_row);
        png_write_row(png_ptr, (png_const_bytep)final);
        lastprog=i/100; if(lastprog!=prog){ prog=lastprog; laShowProgress(-1,(real)i/H); }
    }

    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    if(!SegmentH){ if(Our->ImageBuffer){ free(Our->ImageBuffer); Our->ImageBuffer=0; } }

    if(WriteToBuffer){ *buf=LayerWrite.data; *sizeof_buf=LayerWrite.NextData; }

    free(temp_row);

    return 1;
}
void our_EnsureImageBufferOnRead(OurLayer*l, int W, int H, int UseOffsets, int StartX, int StartY){
    int tw=W/OUR_TILE_W_USE, th=H/OUR_TILE_W_USE;
    int w=tw*OUR_TILE_W_USE, h=th*OUR_TILE_W_USE;
    if(w<W){ tw+=1; w+=OUR_TILE_W_USE; } if(h<H){ th+=1; h+=OUR_TILE_W_USE; }

    int ix=UseOffsets?StartX:(-tw/2*OUR_TILE_W_USE-OUR_TILE_W_USE/2);
    int iy=UseOffsets?StartY:(th/2*OUR_TILE_W_USE+OUR_TILE_W_USE/2);
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
int our_PeekPNG(FILE* fp, int* HasProfile, int* HassRGB, laSafeString** iccName){
    png_structp png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING,0,0,0); if (!png_ptr) { return 0; }
    png_infop info_ptr = png_create_info_struct(png_ptr); if (!info_ptr) { return 0; }
    png_init_io(png_ptr, fp);
    png_read_info(png_ptr, info_ptr);
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
    if(png_get_iCCP (png_ptr, info_ptr, &icc_profile_name, &icc_compression_type, &icc_profile, &icc_proflen)) {
        input_buffer_profile = cmsOpenProfileFromMem(icc_profile, icc_proflen);
        if(!input_buffer_profile) { goto cleanup_png_peek; }
        cmsColorSpaceSignature cs_sig = cmsGetColorSpace(input_buffer_profile);
        if (cs_sig != cmsSigRgbData) { logPrint("    png has grayscale iCCP, Our Paint doesn't supported that yet, will load as sRGB.\n");
            cmsCloseProfile(input_buffer_profile); input_buffer_profile = NULL; }
        else{
            char* desc="UNAMED PROFILE";
            cmsUInt32Number len=cmsGetProfileInfoASCII(input_buffer_profile,cmsInfoDescription,"en","US",0,0);
            if(len){ desc=calloc(1,sizeof(char)*len); cmsGetProfileInfoASCII(input_buffer_profile,cmsInfoDescription,"en","US",desc,len); }
            logPrint("    png has iCCP: %s.\n", desc); strSafeSet(iccName, desc); if(len){ free(desc); } *HasProfile=1;
        }
    }elif(png_get_sRGB(png_ptr,info_ptr,&srgb_intent)){
        logPrint("    png is sRGB.\n");
        *HassRGB=1;
    }else{
        // should use png_get_cHRM and png_get_gAMA, but for simplicity we just treat them as srgb,
        logPrint("    png doesn't contain iCCP or sRGB flags, assuming sRGB.\n");
        *HassRGB=0;
    }

cleanup_png_peek:

    if(input_buffer_profile) cmsCloseProfile(input_buffer_profile);
    if(png_ptr && info_ptr) png_destroy_read_struct(&png_ptr,&info_ptr,0);
    return 1;
}
int our_LayerImportPNG(OurLayer* l, FILE* fp, void* buf, int InputProfileMode, int OutputProfileMode, int UseOffsets, int StartX, int StartY, int NoEnsure){
    int result=0;
    if((!fp&&!buf) || !l) return 0;

    int srgb_intent = 0;
    png_charp icc_profile_name = NULL;
    png_uint_32 icc_proflen = 0;
    int icc_compression_type = 0;
    cmsHPROFILE input_buffer_profile = NULL;
    cmsHPROFILE output_buffer_profile = NULL;
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

    if(InputProfileMode==1){
        if(png_get_iCCP (png_ptr, info_ptr, &icc_profile_name, &icc_compression_type, &icc_profile, &icc_proflen)) {
            input_buffer_profile = cmsOpenProfileFromMem(icc_profile, icc_proflen);
            if(!input_buffer_profile) { goto cleanup_png_read; }
            cmsColorSpaceSignature cs_sig = cmsGetColorSpace(input_buffer_profile);
            if (cs_sig != cmsSigRgbData) { /*no grayscale icc*/ cmsCloseProfile(input_buffer_profile); input_buffer_profile = NULL; }
            else{
                char* desc="UNAMED PROFILE";
                cmsUInt32Number len=cmsGetProfileInfoASCII(input_buffer_profile,cmsInfoDescription,"en","US",0,0);
                if(len){ desc=calloc(1,sizeof(char)*len); cmsGetProfileInfoASCII(input_buffer_profile,cmsInfoDescription,"en","US",desc,len); free(desc); }
            }
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

    int W = png_get_image_width(png_ptr, info_ptr);
    int H = png_get_image_height(png_ptr, info_ptr);

    if(!NoEnsure){ our_EnsureImageBufferOnRead(l,W,H,UseOffsets,StartX,StartY); }
    int LoadY=NoEnsure?StartY:Our->LoadY;

    int prog=0,lastprog=0;
    for(int i=0;i<H;i++){
        png_read_row(png_ptr, &Our->ImageBuffer[((int64_t)(H-i-1+LoadY)*Our->ImageW+Our->LoadX)*4], NULL);
        lastprog=i/100; if(lastprog!=prog){ prog=lastprog; laShowProgress(-1,(real)i/H); }
    }

    if(InputProfileMode && OutputProfileMode && (InputProfileMode!=OutputProfileMode)){
        void* icc=0; int iccsize=0;
        if(!input_buffer_profile){
            if(InputProfileMode==OUR_PNG_READ_INPUT_SRGB){ icc=Our->icc_sRGB; iccsize=Our->iccsize_sRGB; }
            elif(InputProfileMode==OUR_PNG_READ_INPUT_CLAY){ icc=Our->icc_Clay; iccsize=Our->iccsize_Clay; }
            elif(InputProfileMode==OUR_PNG_READ_INPUT_D65_P3){ icc=Our->icc_D65P3; iccsize=Our->iccsize_D65P3; }
            elif(InputProfileMode==OUR_PNG_READ_INPUT_LINEAR_SRGB){ icc=Our->icc_LinearsRGB; iccsize=Our->iccsize_LinearsRGB; }
            elif(InputProfileMode==OUR_PNG_READ_INPUT_LINEAR_CLAY){ icc=Our->icc_LinearClay; iccsize=Our->iccsize_LinearClay; }
            elif(InputProfileMode==OUR_PNG_READ_INPUT_LINEAR_D65_P3){ icc=Our->icc_LinearD65P3; iccsize=Our->iccsize_LinearD65P3; }
            input_buffer_profile=cmsOpenProfileFromMem(icc, iccsize);
        }
        icc=0; iccsize=0;
        if(OutputProfileMode==OUR_PNG_READ_OUTPUT_LINEAR_SRGB){ icc=Our->icc_LinearsRGB; iccsize=Our->iccsize_LinearsRGB; }
        elif(OutputProfileMode==OUR_PNG_READ_OUTPUT_LINEAR_CLAY){ icc=Our->icc_LinearClay; iccsize=Our->iccsize_LinearClay; }
        elif(OutputProfileMode==OUR_PNG_READ_OUTPUT_LINEAR_D65_P3){ icc=Our->icc_LinearD65P3; iccsize=Our->iccsize_LinearD65P3; }
        output_buffer_profile=cmsOpenProfileFromMem(icc, iccsize);
        if(input_buffer_profile && output_buffer_profile){
            cmsTransform = cmsCreateTransform(input_buffer_profile, TYPE_RGBA_16, output_buffer_profile, TYPE_RGBA_16, INTENT_PERCEPTUAL, 0);
            cmsDoTransform(cmsTransform,Our->ImageBuffer,Our->ImageBuffer,(int64_t)Our->ImageW*Our->ImageH);
        }
    }

    if(!NoEnsure){
        LA_ACQUIRE_GLES_CONTEXT;
        our_ImageBufferToNative();
        our_LayerToTexture(l);
    }

    result=1;

cleanup_png_read:

    if(input_buffer_profile) cmsCloseProfile(input_buffer_profile);
    if(output_buffer_profile) cmsCloseProfile(output_buffer_profile);
    if(cmsTransform) cmsDeleteTransform(cmsTransform);
    if(png_ptr && info_ptr) png_destroy_read_struct(&png_ptr,&info_ptr,0);
    if(!NoEnsure){ if(Our->ImageBuffer){ free(Our->ImageBuffer); Our->ImageBuffer=0; } }

    return result;
}

void our_UiToCanvas(laCanvasExtra* ex, laEvent*e, real* x, real *y){
    *x = (real)((real)e->x - (real)(ex->ParentUi->R - ex->ParentUi->L) / 2 - ex->ParentUi->L) * ex->ZoomX + ex->PanX;
    *y = (real)((real)(ex->ParentUi->B - ex->ParentUi->U) / 2 - (real)e->y + ex->ParentUi->U) * ex->ZoomY + ex->PanY;
}
void our_PaintResetBrushState(OurBrush* b){
    b->BrushRemainingDist = 0; b->SmudgeAccum=0; b->SmudgeRestart=1;
    Our->LastBrushCenter[0]=-1e21;
}
real our_PaintGetDabStepDistance(real Size,real DabsPerSize){
    real d=Size/DabsPerSize; if(d<1e-2) d=1e-2; return d;
}
int our_PaintGetDabs(OurBrush* b, OurLayer* l, real x, real y, real xto, real yto,
    real last_pressure, real last_orientation, real last_deviation, real last_twist, real pressure, real Orientation, real Deviation, real Twist,
    int *tl, int *tr, int* tu, int* tb, real* r_xto, real* r_yto){
    if (isnan(x)||isnan(y)||isnan(xto)||isnan(yto)||isinf(x)||isinf(y)||isinf(xto)||isinf(yto)){
        printf("brush input coordinates has nan or inf.\n"); return 0;
    }
    Our->NextDab=0;
    if(!b->EvalDabsPerSize) b->EvalDabsPerSize=b->DabsPerSize;
    real smfac=(1-b->Smoothness/1.1); xto=tnsLinearItp(x,xto,smfac); yto=tnsLinearItp(y,yto,smfac);  *r_xto=xto; *r_yto=yto;
    real dd=our_PaintGetDabStepDistance(b->EvalSize, b->EvalDabsPerSize); real len=tnsDistIdv2(x,y,xto,yto); real rem=b->BrushRemainingDist;
    if(len>1000){ *r_xto=xto; *r_yto=yto; b->BrushRemainingDist=0; return 0; /* Prevent crazy events causing GPU hang. */ }
    real alllen=len+rem; real uselen=dd,step=0; if(!len)return 0; if(dd>alllen){ b->BrushRemainingDist+=len; return 0; }
    real xmin=FLT_MAX,xmax=-FLT_MAX,ymin=FLT_MAX,ymax=-FLT_MAX;
    real bsize=OUR_BRUSH_ACTUAL_SIZE(b);
    b->EvalSize=bsize; b->EvalHardness=b->Hardness; b->EvalSmudge=b->Smudge; b->EvalSmudgeLength=b->SmudgeResampleLength;
    b->EvalTransparency=b->Transparency; b->EvalDabsPerSize=b->DabsPerSize; b->EvalSlender=b->Slender; b->EvalAngle=b->Angle;
    b->EvalSpeed=tnsDistIdv2(x,y,xto,yto)/bsize; b->EvalForce=b->Force; b->EvalGunkyness=b->Gunkyness;
    if(Our->ResetBrush){ b->LastX=x; b->LastY=y; b->LastAngle=atan2(yto-y,xto-x); b->EvalStrokeLength=0; Our->ResetBrush=0; }
    real this_angle=atan2(yto-y,xto-x);
    if(b->LastAngle-this_angle>TNS_PI){ this_angle+=(TNS_PI*2); }
    elif(this_angle-b->LastAngle>TNS_PI){ b->LastAngle+=(TNS_PI*2); }

    while(1){ int Repeat=1; OurDab* od;
        for(b->Iteration=0;b->Iteration<Repeat;b->Iteration++){ b->EvalDiscard=0;
            arrEnsureLength(&Our->Dabs,Our->NextDab,&Our->MaxDab,sizeof(OurDab)); od=&Our->Dabs[Our->NextDab]; od->Direction[0]=-1e21;
            real r=tnsGetRatiod(0,len,uselen-rem); od->X=tnsInterpolate(x,xto,r); od->Y=tnsInterpolate(y,yto,r); TNS_CLAMP(r,0,1);
            b->LastX=od->X; b->LastY=od->Y; tnsVectorSet3v(b->EvalColor, Our->CurrentColor);
            if(b->UseNodes){
                b->EvalPressure=tnsInterpolate(last_pressure,pressure,r); b->EvalPosition[0]=od->X; b->EvalPosition[1]=od->Y;
                b->EvalOffset[0]=0; b->EvalOffset[1]=0; b->EvalStrokeAngle=tnsInterpolate(b->LastAngle,this_angle,r);
                b->EvalTilt[0]=tnsInterpolate(last_orientation,Orientation,r); b->EvalTilt[1]=tnsInterpolate(last_deviation,Deviation,r);
                b->EvalTwist=tnsInterpolate(last_twist,Twist,r);
                ourEvalBrush();  if(!b->Iteration){ Repeat=b->EvalRepeats;} if(b->EvalDiscard){ continue; }
                TNS_CLAMP(b->EvalSmudge,0,1); TNS_CLAMP(b->EvalSmudgeLength,0,100000); TNS_CLAMP(b->EvalTransparency,0,1); TNS_CLAMP(b->EvalHardness,0,1);  TNS_CLAMP(b->DabsPerSize,0,100000);
                od->X+=b->EvalOffset[0]; od->Y+=b->EvalOffset[1];
            }
            if(!b->EvalDabsPerSize) b->EvalDabsPerSize=1;
    #define pfac(psw) (((!b->UseNodes)&&psw)?tnsInterpolate(last_pressure,pressure,r):1)
            od->Size = b->EvalSize*pfac(b->PressureSize);       od->Hardness = b->EvalHardness*pfac(b->PressureHardness);
            od->Smudge = b->EvalSmudge*pfac(b->PressureSmudge); od->Color[3]=pow(b->EvalTransparency*pfac(b->PressureTransparency),2.718);
            tnsVectorSet3v(od->Color,b->EvalColor);             od->Force=b->EvalForce*pfac(b->PressureForce);
    #undef pfac;
            od->Gunkyness = b->EvalGunkyness; od->Slender = b->EvalSlender;
            od->Angle=b->EvalAngle; if(b->TwistAngle){ od->Angle=tnsInterpolate(last_twist,Twist,r); }
            xmin=TNS_MIN2(xmin, od->X-od->Size); xmax=TNS_MAX2(xmax, od->X+od->Size); 
            ymin=TNS_MIN2(ymin, od->Y-od->Size); ymax=TNS_MAX2(ymax, od->Y+od->Size);
            if(od->Size>1e-1 && (!b->EvalDiscard)) Our->NextDab++;
        }
        step=our_PaintGetDabStepDistance(od->Size, b->EvalDabsPerSize);
        b->EvalStrokeLength+=step/bsize; b->EvalStrokeLengthAccum+=step/bsize; if(b->EvalStrokeLengthAccum>1e6){b->EvalStrokeLengthAccum-=1e6;}
        od->ResampleSmudge=0;
        if(b->Smudge>1e-3){ b->SmudgeAccum+=step;
            if(b->SmudgeAccum>(b->EvalSmudgeLength*od->Size)){ b->SmudgeAccum-=(b->EvalSmudgeLength*od->Size); od->ResampleSmudge=1; }
            od->Recentness=b->SmudgeAccum/b->EvalSmudgeLength/od->Size; TNS_CLAMP(od->Recentness,0,1);
        }else{od->Recentness=0;}
        if(step+uselen<alllen)uselen+=step; else break;
    }
    if(this_angle>TNS_PI*2){ this_angle-=(TNS_PI*2); }
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
    float center[2]; center[0]=d->X-tl; center[1]=d->Y-tu;
    if(d->Direction[0]<-1e20){
        if(Our->LastBrushCenter[0]<-1e20){ d->Direction[0]=0;d->Direction[1]=0; }
        else{ d->Direction[0]=d->X-Our->LastBrushCenter[0]; d->Direction[1]=d->Y-Our->LastBrushCenter[1]; }
    } tnsVectorSet2(Our->LastBrushCenter,d->X,d->Y);
    if(corner[0]>tr||MaxX<tl||corner[1]>tb||MaxY<tu) return;
    corner[0]=corner[0]-tl; corner[1]=corner[1]-tu;
    glUniform2iv(Our->uBrushCorner,1,corner);
    glUniform2fv(Our->uBrushCenter,1,center);
    glUniform1f(Our->uBrushSize,d->Size);
    glUniform1f(Our->uBrushHardness,d->Hardness);
    glUniform1f(Our->uBrushSmudge,d->Smudge);
    glUniform1f(Our->uBrushSlender,d->Slender);
    glUniform1f(Our->uBrushAngle,d->Angle);
    glUniform2fv(Our->uBrushDirection,1,d->Direction);
    glUniform1f(Our->uBrushForce,d->Force);
    glUniform1f(Our->uBrushGunkyness,d->Gunkyness);
    glUniform1f(Our->uBrushRecentness,d->Recentness);
    glUniform4fv(Our->uBrushColor,1,d->Color);
    GLuint compute_dimension = ceil((d->Size+2)*2/OUR_WORKGROUP_SIZE);
    glDispatchCompute(compute_dimension, compute_dimension, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}
void our_PaintDoDabs(OurLayer* l,int tl, int tr, int tu, int tb, int Start, int End){
    for(int row=tb;row<=tu;row++){
        for(int col=tl;col<=tr;col++){
            OurTexTile* ott=l->TexTiles[row][col];
            glBindImageTexture(0, ott->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, OUR_CANVAS_GL_PIX);
            int s[2]; s[0]=l->TexTiles[row][col]->l,s[1]=l->TexTiles[row][col]->b;
            glUniform2iv(Our->uImageOffset,1,s);
            for(int i=Start;i<End;i++){
                our_PaintDoDab(&Our->Dabs[i],s[0],s[0]+OUR_TILE_W,s[1],s[1]+OUR_TILE_W);
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
    OurSmudgeSegement* oss; unsigned int uniforms[2];
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
    glUniform1i(Our->uBrushMix,Our->Erasing?0:Our->BrushMix);
#ifdef LA_USE_GLES
    glUniform1i(Our->uBrushRoutineSelectionES,0);
    glUniform1i(Our->uMixRoutineSelectionES,Our->SpectralMode?1:0);
#else
    uniforms[Our->uBrushRoutineSelection]=Our->RoutineDoDabs;
    uniforms[Our->uMixRoutineSelection]=Our->SpectralMode?Our->RoutineDoMixSpectral:Our->RoutineDoMixNormal;
    glUniformSubroutinesuiv(GL_COMPUTE_SHADER,2,uniforms);
#endif
    glUniform1i(Our->uCanvasType,Our->BackgroundType);
    glUniform1i(Our->uCanvasRandom,Our->BackgroundRandom);
    glUniform1f(Our->uCanvasFactor,Our->BackgroundFactor);

    while(oss=lstPopItem(&Segments)){
        if(oss->Resample || Our->CurrentBrush->SmudgeRestart){
            uniforms[Our->uBrushRoutineSelection]=Our->RoutineDoSample;
#ifdef LA_USE_GLES
            glUniform1i(Our->uBrushRoutineSelectionES,1);
#else
            glUniformSubroutinesuiv(GL_COMPUTE_SHADER,2,uniforms);
#endif
            int x=Our->Dabs[oss->Start].X, y=Our->Dabs[oss->Start].Y; float usize=Our->Dabs[oss->Start].Size;
            float ssize=(usize>15)?(usize+1.5):(usize*1.1); if(ssize<3) ssize=3;
            int colmax=(int)(floor(OUR_TILE_CTR+(float)(x+ssize)/OUR_TILE_W_USE+0.5)); TNS_CLAMP(colmax,0,OUR_TILES_PER_ROW-1);
            int rowmax=(int)(floor(OUR_TILE_CTR+(float)(y+ssize)/OUR_TILE_W_USE+0.5)); TNS_CLAMP(rowmax,0,OUR_TILES_PER_ROW-1);
            int colmin=(int)(floor(OUR_TILE_CTR+(float)(x-ssize)/OUR_TILE_W_USE+0.5)); TNS_CLAMP(colmin,0,OUR_TILES_PER_ROW-1);
            int rowmin=(int)(floor(OUR_TILE_CTR+(float)(y-ssize)/OUR_TILE_W_USE+0.5)); TNS_CLAMP(rowmin,0,OUR_TILES_PER_ROW-1);
            glBindImageTexture(1, Our->SmudgeTexture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, OUR_CANVAS_GL_PIX);
            for(int col=colmin;col<=colmax;col++){
                for(int row=rowmin;row<=rowmax;row++){
                    glBindImageTexture(0, l->TexTiles[row][col]->Texture->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, OUR_CANVAS_GL_PIX);
                    int sx=l->TexTiles[row][col]->l,sy=l->TexTiles[row][col]->b;
                    our_PaintDoSample(x,y,sx,sy,ssize,(col==colmax)&&(row==rowmax),Our->CurrentBrush->SmudgeRestart);
                }
            }
            Our->CurrentBrush->SmudgeRestart=0;
            uniforms[Our->uBrushRoutineSelection]=Our->RoutineDoDabs;
#ifdef LA_USE_GLES
            glUniform1i(Our->uBrushRoutineSelectionES,0);
#else
            glUniformSubroutinesuiv(GL_COMPUTE_SHADER,2,uniforms);
#endif
            glUniform1i(Our->uBrushErasing,Our->Erasing);
        }

        //printf("from to %d %d %d\n", oss->Start,oss->End,Our->Dabs[oss->Start].ResampleSmudge);

        our_PaintDoDabs(l,tl,tr,tu,tb,oss->Start,oss->End);
    }
}
void ourset_CurrentBrush(void* unused, OurBrush* b);
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
    float color[4]; real rcolor[3],xyz[3];
    glBindFramebuffer(GL_READ_FRAMEBUFFER, e->OffScr->FboHandle);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glReadPixels(x,y,1,1, GL_RGBA, GL_FLOAT, color);
    color[0]*=color[3];color[1]*=color[3];color[2]*=color[3];tnsVectorSet3v(Our->CurrentColor,color);
}

int our_RenderThumbnail(uint8_t** buf, int* sizeof_buf){
    int x=INT_MAX,y=INT_MAX,x1=-INT_MAX,y1=-INT_MAX,w=-INT_MAX,h=-INT_MAX;
    for(OurLayer* l=Our->Layers.pFirst;l;l=l->Item.pNext){
        our_LayerClearEmptyTiles(l);
        our_LayerEnsureImageBuffer(l,1);
        if(Our->ImageX<x) x=Our->ImageX; if(Our->ImageY<y) y=Our->ImageY;
        if(Our->ImageW+Our->ImageX>x1) x1=Our->ImageW+Our->ImageX;
        if(Our->ImageH+Our->ImageY>y1) y1=Our->ImageH+Our->ImageY;
    }
    w = x1-x; h=y1-y;
    if(w<=0||h<=0) return 0;
    real r = (real)(TNS_MAX2(w,h))/400.0f;
    int use_w=w/r, use_h=h/r;

    tnsOffscreen* off = tnsCreate2DOffscreen(GL_RGBA,use_w,use_h,0,0,0);
    tnsDrawToOffscreen(off,1,0);
    tnsViewportWithScissor(0, 0, use_w, use_h);
    tnsResetViewMatrix();tnsResetModelMatrix();tnsResetProjectionMatrix();
    tnsOrtho(x,x+w,y+h,y,-100,100);
    tnsClearColor(LA_COLOR3(Our->BackgroundColor),1); tnsClearAll();
    our_CanvasDrawTextures();

    if(Our->ImageBuffer){ free(Our->ImageBuffer); }
    int bufsize=use_w*use_h*OUR_CANVAS_PIXEL_SIZE;
    Our->ImageBuffer=malloc(bufsize);
    tnsBindTexture(off->pColor[0]); glPixelStorei(GL_PACK_ALIGNMENT, 1);
#ifdef LA_USE_GLES
    int readtype=GL_UNSIGNED_BYTE;
#else
    int readtype=GL_UNSIGNED_SHORT;
#endif
    tnsGet2DTextureSubImage(off->pColor[0], 0, 0, use_w, use_h, GL_RGBA, readtype, bufsize, Our->ImageBuffer);

    tnsDrawToScreen();
    tnsDelete2DOffscreen(off);

    Our->ImageW = use_w; Our->ImageH = use_h;
    our_ImageBufferFromNative();
    our_ImageConvertForExport(OUR_EXPORT_BIT_DEPTH_8,OUR_EXPORT_COLOR_MODE_CLAY);

    png_structp png_ptr=png_create_write_struct(PNG_LIBPNG_VER_STRING,0,0,0);
    png_infop info_ptr = png_create_info_struct(png_ptr);

    OurLayerWrite LayerWrite={0};
    arrEnsureLength(&LayerWrite.data,0,&LayerWrite.MaxData,sizeof(unsigned char));
    png_set_write_fn(png_ptr,&LayerWrite,_our_png_write,0);

    png_set_IHDR(png_ptr, info_ptr,use_w,use_h,8,PNG_COLOR_TYPE_RGBA,PNG_INTERLACE_NONE,PNG_COMPRESSION_TYPE_BASE,PNG_FILTER_TYPE_BASE);
    png_write_info(png_ptr, info_ptr);
    png_set_swap(png_ptr);

    uint8_t* data=Our->ImageBuffer;
    for(int i=0;i<use_h;i++){
        png_write_row(png_ptr,&data[use_w*i*4]);
    }
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);

    *buf=LayerWrite.data; *sizeof_buf=LayerWrite.NextData;

    free(Our->ImageBuffer); Our->ImageBuffer=0;

    Our->ImageW = w; Our->ImageH = h;
    return 1;
}
int our_GetFileThumbnail(char* file, uint8_t** buf, int* sizeof_buf){
    laUDF* udf = laOpenUDF(file,0,0,0);

    if(udf){
        laExtractProp(udf,"our.thumbnail");
        laExtractUDF(udf,0,LA_UDF_MODE_APPEND); laCloseUDF(udf);
    }
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

void our_LayerGetRange(OurLayer* ol, int* rowmin,int* rowmax, int* colmin, int* colmax){
    *rowmin = *colmin = INT_MAX; *rowmax = *colmax = -INT_MAX;
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        if(*rowmin==INT_MAX){ *rowmin = row; }
        *rowmax=row;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue;
            if(col > *colmax){ *colmax=col; }
            if(col < *colmin){ *colmin = col; }
        }
    }
}
int our_MoveLayer(OurLayer* ol, int dx, int dy, int* movedx, int* movedy){
    if(!ol || (!dx && !dy)){ *movedx=0; *movedy=0; return 0; }
    int rowmin,rowmax,colmin,colmax; our_LayerGetRange(ol,&rowmin,&rowmax,&colmin,&colmax);

    if(dx+colmax >= OUR_TILES_PER_ROW){ dx = OUR_TILES_PER_ROW - colmax - 1; }
    if(dy+rowmax >= OUR_TILES_PER_ROW){ dy = OUR_TILES_PER_ROW - rowmax - 1; }
    if(colmin + dx < 0){ dx = -colmin; }
    if(rowmin + dy < 0){ dy = -rowmin; }

    if(!dx && !dy){ *movedx=0; *movedy=0; return 0; }

    if(movedx){ *movedx=dx; }
    if(movedy){ *movedy=dy; }

    OurTexTile*** copy = memAcquire(sizeof(void*)*OUR_TILES_PER_ROW);
    for(int i=0;i<OUR_TILES_PER_ROW;i++){ copy[i] = memAcquire(sizeof(void*)*OUR_TILES_PER_ROW); }
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){
            copy[row][col] = ol->TexTiles[row][col];
            ol->TexTiles[row][col] = 0;
        }
    }
    for(int row=rowmin;row<=rowmax;row++){ if(!ol->TexTiles[row]) continue;
        for(int col=colmin;col<=colmax;col++){
            OurTexTile* t0=copy[row][col]; if(!t0){continue; }
            t0->l+=dx*OUR_TILE_W; t0->r+=dx*OUR_TILE_W;
            t0->u+=dy*OUR_TILE_W; t0->b+=dy*OUR_TILE_W;
            if(!ol->TexTiles[row+dy]){
                ol->TexTiles[row+dy] = memAcquire(sizeof(OurTexTile*)*OUR_TILES_PER_ROW);
            }
            ol->TexTiles[row+dy][col+dx] = t0;
        }
    }
    for(int i=0;i<OUR_TILES_PER_ROW;i++){ memFree(copy[i]); }
    memFree(copy);
    
    return 1;
}
void ourundo_Move(OurMoveUndo* undo){
    our_LayerClearEmptyTiles(undo->Layer);
    our_MoveLayer(undo->Layer, -undo->dx, -undo->dy,0,0);
    laNotifyUsers("our.canvas_notify");
}
void ourredo_Move(OurMoveUndo* undo){
    our_LayerClearEmptyTiles(undo->Layer);
    our_MoveLayer(undo->Layer, undo->dx, undo->dy,0,0);
    laNotifyUsers("our.canvas_notify");
}
void our_DoMoving(OurCanvasDraw* cd, real x, real y, int *movedx, int *movedy){
    OurLayer* ol=Our->CurrentLayer; if(!ol){ return; }
    int dx = x-cd->CanvasDownX, dy = y-cd->CanvasDownY;
    dx/=OUR_TILE_W_USE; dy/=OUR_TILE_W_USE;
    if(dx || dy){
        our_MoveLayer(ol, dx,dy,movedx,movedy);
        laNotifyUsers("our.canvas_notify");
        cd->CanvasDownX+=dx*OUR_TILE_W_USE; cd->CanvasDownY+=dy*OUR_TILE_W_USE;
    }
}

void our_ShowAllocationError(laEvent* e){
    char buf[256];
    Our->SaveFailed=1;
    sprintf(buf, "%s %dx%d.\n",transLate("Can't allocate memory for size"),Our->ImageW,Our->ImageH);
    logPrintNew("Export: %s",buf);
    if(e){
        strcat(buf,transLate("Try erasing some contents to make the canvas smaller.\n"));
        laEnableMessagePanel(0,0,"Export Error",buf,e->x,e->y,200,e);
    }
}

int ourinv_ShowSplash(laOperator* a, laEvent* e){
    our_EnableSplashPanel();return LA_FINISHED;
}

int ourinv_NewLayer(laOperator* a, laEvent* e){
    our_NewLayer("Our Layer"); laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    laRecordDifferences(0,"our.canvas.layers");laRecordDifferences(0,"our.canvas.current_layer");laPushDifferences("New Layer",0);
    return LA_FINISHED;
}
int ourinv_DuplicateLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:Our->CurrentLayer;
    if(!l){ return LA_FINISHED; }
    our_NewLayer(SSTR(Our->CurrentLayer->Name));
    our_DuplicateLayerContent(Our->CurrentLayer,l);

    int rowmin,rowmax,colmin,colmax;
    our_LayerGetRange(Our->CurrentLayer,&rowmin,&rowmax,&colmin,&colmax);
    int xmin,xmax,ymin,ymax;
    xmin =((real)colmin-OUR_TILE_CTR-0.5)*OUR_TILE_W_USE; ymin=((real)rowmin-OUR_TILE_CTR-0.5)*OUR_TILE_W_USE;
    xmax =((real)colmax+1-OUR_TILE_CTR+0.5)*OUR_TILE_W_USE; ymax=((real)rowmax+1-OUR_TILE_CTR-0.5)*OUR_TILE_W_USE;
    our_RecordUndo(Our->CurrentLayer,xmin,xmax,ymin,ymax,1,0);
    laRecordDifferences(0,"our.canvas.layers");laRecordDifferences(0,"our.canvas.current_layer");laPushDifferences("New Layer",0);
    
    laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    return LA_FINISHED;
}
int ourinv_RemoveLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l) return LA_CANCELED;
    our_RemoveLayer(l,0); laNotifyUsers("our.canvas.layers"); laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    laRecordDifferences(0,"our.canvas.layers");laRecordDifferences(0,"our.canvas.current_layer");laPushDifferences("Remove Layer",0);
    return LA_FINISHED;
}
int ourinv_MoveLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l) return LA_CANCELED; int changed=0;
    char* direction=strGetArgumentString(a->ExtraInstructionsP,"direction");
    if(strSame(direction,"up")&&l->Item.pPrev){ lstMoveUp(&Our->Layers, l); changed=1; }
    elif(l->Item.pNext){ lstMoveDown(&Our->Layers, l); changed=1; }
    if(changed){ laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst); laRecordDifferences(0,"our.canvas.layers"); laPushDifferences("Move Layer",0); }
    return LA_FINISHED;
}
int ourchk_MergeLayer(laPropPack *This, laStringSplitor *ss){
    OurLayer* l=This->EndInstance; if(!l || !l->Item.pNext) return 0;
    OurLayer* nl=l->Item.pNext; if(l->Lock || l->Transparency==1 || nl->Lock || nl->Transparency==1) return 0;
    return 1;
}
int ourinv_MergeLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l || !l->Item.pNext) return LA_CANCELED;
    OurLayer* nl=l->Item.pNext; if(l->Lock || l->Transparency==1 || nl->Lock || nl->Transparency==1) return LA_CANCELED;
    if(our_MergeLayer(l)){ laNotifyUsers("our.canvas"); laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst); }
    return LA_FINISHED;
}
int ourchk_ExportLayer(laPropPack *This, laStringSplitor *ss){
    OurLayer* ol=This?This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return 0; return 1;
}
int ourinv_ExportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    laInvoke(a, "LA_file_dialog", e, 0, "warn_file_exists=true;filter_extensions=png;use_extension=png", 0);
    return LA_RUNNING;
}
int ourmod_ExportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    if (a->ConfirmData){
        if (a->ConfirmData->StrData){
            our_LayerClearEmptyTiles(ol);
            int ensure = our_LayerEnsureImageBuffer(ol, 0);
            if(!ensure){ our_ShowAllocationError(e); return LA_FINISHED; }
            if(ensure<0){ return LA_FINISHED; }
            FILE* fp=fopen(a->ConfirmData->StrData,"wb");
            if(!fp) return LA_FINISHED;
            laShowProgress(0,-1);
            our_LayerToImageBuffer(ol, 0);
            laShowProgress(0.5,-1);
            our_ImageBufferFromNative();
            our_ImageExportPNG(fp, 0, 0, 0, 0, OUR_EXPORT_BIT_DEPTH_16, OUR_EXPORT_COLOR_MODE_FLAT,0,0);
            if(Our->ImageBuffer){ free(Our->ImageBuffer); Our->ImageBuffer=0; }
            laHideProgress();
            fclose(fp);
        }
        return LA_FINISHED;
    }
    return LA_RUNNING;
}
int ourinv_ImportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0;
    a->CustomData=memAcquire(sizeof(OurPNGReadExtra));
    laInvoke(a, "LA_file_dialog", e, 0, "filter_extensions=png;use_extension=png", 0);
    return LA_RUNNING;
}
int ourmod_ImportLayer(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0;
    OurPNGReadExtra* ex=a->CustomData;
    if(!ex->Confirming){
        if (a->ConfirmData){
            if (a->ConfirmData->StrData){
                FILE* fp=fopen(a->ConfirmData->StrData,"rb"); if(!fp) return LA_FINISHED;
                if(!our_PeekPNG(fp,&ex->HasProfile, &ex->HassRGB, &ex->iccName)){ fclose(fp); return LA_FINISHED; }
                else{ ex->Confirming=1; fclose(fp); strSafeSet(&ex->FilePath,a->ConfirmData->StrData);
                    if(ex->HasProfile){ex->InputMode=OUR_PNG_READ_INPUT_ICC;}
                    else{ ex->InputMode=OUR_PNG_READ_INPUT_SRGB; }
                    laEnableOperatorPanel(a,a->This,e->x,e->y,300,200,0,0,0,0,0,0,0,0,e); return LA_RUNNING;
                }
            }
            return LA_FINISHED;
        }
    }else{
        if (a->ConfirmData){
            if (a->ConfirmData->Mode==LA_CONFIRM_OK){
                FILE* fp=fopen(ex->FilePath->Ptr,"rb"); if(!fp) return LA_FINISHED;
                if(!ol) ol=our_NewLayer("Imported");
                int OutMode=ex->OutputMode?ex->OutputMode:((Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_SRGB)?OUR_PNG_READ_OUTPUT_LINEAR_SRGB:
                                                           (Our->ColorInterpretation==OUR_CANVAS_INTERPRETATION_D65_P3?OUR_PNG_READ_INPUT_D65_P3:OUR_PNG_READ_OUTPUT_LINEAR_CLAY));
                int UseOffsets = ex->Offsets[0] && ex->Offsets[1];
                our_LayerImportPNG(ol, fp, 0, ex->InputMode, OutMode, UseOffsets, ex->Offsets[0], ex->Offsets[1],0);
                laNotifyUsers("our.canvas"); laNotifyUsers("our.canvas.layers"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
                laRecordDifferences(0,"our.canvas.layers");laRecordDifferences(0,"our.canvas.current_layer");laPushDifferences("New Layer",0);
                our_LayerRefreshLocal(ol);
                laHideProgress();
                fclose(fp);
            }
            return LA_FINISHED;
        }
    }
    return LA_RUNNING;
}
void ourexit_ImportLayer(laOperator* a, int result){
    OurPNGReadExtra* ex=a->CustomData;
    strSafeDestroy(&ex->FilePath);
    memFree(ex);
}
void ourui_ImportLayer(laUiList *uil, laPropPack *This, laPropPack *Operator, laColumn *UNUSED, int context){
    laColumn* c = laFirstColumn(uil),*cl,*cr; laSplitColumn(uil,c,0.5);cl=laLeftColumn(c,0);cr=laRightColumn(c,0);
    laUiItem* b;

    laShowLabel(uil,c,"Select the importing behavior:",0,0);
    laShowLabel(uil,cl,"Input:",0,0);  laShowItem(uil,cl,Operator,"input_mode");
    b=laOnConditionThat(uil,c,laNot(laEqual(laPropExpression(Operator,"input_mode"),laIntExpression(OUR_PNG_READ_INPUT_FLAT))));{
        laShowLabel(uil,cr,"Output:",0,0); laShowItem(uil,cr,Operator,"output_mode");
        laShowLabel(uil,cl,"Canvas:",0,0)->Flags|=LA_TEXT_ALIGN_RIGHT; laShowItem(uil,cr,0,"our.canvas.color_interpretation");
    }laEndCondition(uil,b);
    b=laOnConditionThat(uil,c,laPropExpression(Operator,"has_profile"));{
        laShowLabel(uil,c,"Input image has built-in color profile:",0,0);
        laShowItem(uil,cl,Operator,"icc_name")->Flags|=LA_TEXT_MONO;
    }laElse(uil,b);{
        laShowLabel(uil,c,"Input image does not have a built-in color profile.",0,0)->Flags|=LA_UI_FLAGS_DISABLED;
    }laEndCondition(uil,b);
    b=laOnConditionThat(uil,c,laPropExpression(Operator,"has_srgb"));{
        laShowLabel(uil,c,"Input image is tagged as sRGB.",0,0);
    }laElse(uil,b);{
        laShowLabel(uil,c,"Input image is not tagged as sRGB.",0,0)->Flags|=LA_UI_FLAGS_DISABLED;
    }laEndCondition(uil,b);
    laUiItem* row = laBeginRow(uil,cl,0,0);
    laShowLabel(uil,cl,"Use Offsets",0,0);
    b=laOnConditionToggle(uil,cl,0,0,0,0,0);{
        laEndRow(uil,row);
        laShowItem(uil,cl,Operator,"offsets");
    }laEndCondition(uil,b);
    laEndRow(uil,row);

    b=laBeginRow(uil,c,0,0);laShowSeparator(uil,c)->Expand=1;laShowItem(uil,c,0,"LA_confirm")->Flags|=LA_UI_FLAGS_HIGHLIGHT;laEndRow(uil,b);
}
int ourchk_ExportImage(laPropPack *This, laStringSplitor *ss){
    OurLayer* ol=This?This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return 0; return 1;
}
int ourinv_ExportImage(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    a->CustomData=memAcquire(sizeof(OurPNGWriteExtra));
    laInvoke(a, "LA_file_dialog", e, 0, "warn_file_exists=true;filter_extensions=png;use_extension=png", 0);
    return LA_RUNNING;
}
int ourmod_ExportImage(laOperator* a, laEvent* e){
    OurLayer* ol=a->This?a->This->EndInstance:0; if(!ol) ol=Our->CurrentLayer; if(!ol) return LA_FINISHED;
    OurPNGWriteExtra* ex=a->CustomData;
    if(!ex->Confirming){
       if (a->ConfirmData){
            if (a->ConfirmData->StrData){
                strSafeSet(&ex->FilePath,a->ConfirmData->StrData); ex->Confirming=1;
                ex->ColorProfile=Our->DefaultColorProfile; ex->BitDepth=Our->DefaultBitDepth;
                laEnableOperatorPanel(a,a->This,e->x,e->y,200,200,0,0,0,0,0,0,0,0,e); return LA_RUNNING;
            }
            return LA_FINISHED;
        }
    }else{
         if (a->ConfirmData){
            if (a->ConfirmData->Mode==LA_CONFIRM_OK){
                if(!our_CanvasEnsureImageBuffer()){ our_ShowAllocationError(e); return LA_FINISHED; }
                FILE* fp=fopen(ex->FilePath->Ptr,"wb");
                if(!fp) return LA_FINISHED;
                static int LayerCount=0; static int CurrentLayer=0; LayerCount=lstCountElements(&Our->Layers); CurrentLayer=0;
                our_CanvasFillImageBufferBackground(ex->Transparent);
                laShowProgress(0,-1);
                for(OurLayer* l=Our->Layers.pLast;l;l=l->Item.pPrev){
                    our_LayerToImageBuffer(l, 1);
                    CurrentLayer++; laShowProgress((real)CurrentLayer/LayerCount,-1);
                }
                our_ImageBufferFromNative();
                our_ImageConvertForExport(ex->BitDepth, ex->ColorProfile);
                if(!Our->ImageBuffer){ our_ShowAllocationError(e); fclose(fp); return LA_FINISHED; }
                our_ImageExportPNG(fp, 0, 0, 0, Our->ShowBorder, ex->BitDepth, ex->ColorProfile,0,0);
                if(Our->ImageBuffer){ free(Our->ImageBuffer); Our->ImageBuffer=0; }
                laHideProgress();
                fclose(fp);
            }
            return LA_FINISHED;
        }
    }
    return LA_RUNNING;
}
void ourexit_ExportImage(laOperator* a, int result){
    OurPNGWriteExtra* ex=a->CustomData;
    strSafeDestroy(&ex->FilePath);
    memFree(ex);
}
void ourui_ExportImage(laUiList *uil, laPropPack *This, laPropPack *Operator, laColumn *UNUSED, int context){
    laColumn* c = laFirstColumn(uil),*cl,*cr; laSplitColumn(uil,c,0.5);cl=laLeftColumn(c,0);cr=laRightColumn(c,0);
    laUiItem* b;

    laShowLabel(uil,c,"Select the exporting behavior:",0,0);
    laShowLabel(uil,cl,"Bit Depth:",0,0);  laShowItem(uil,cl,Operator,"bit_depth");
    b=laOnConditionThat(uil,c,laEqual(laPropExpression(Operator,"bit_depth"),laIntExpression(OUR_EXPORT_BIT_DEPTH_16)));{
        laShowLabel(uil,c,"16 bit images would be exported in the same linear color space as the canvas",0,0)->Flags|=LA_UI_FLAGS_DISABLED|LA_TEXT_LINE_WRAP;
    }laElse(uil,b);{
        laShowLabel(uil,cr,"Color Space:",0,0); laShowItem(uil,cr,Operator,"color_profile");
    }laEndCondition(uil,b);

    laShowLabel(uil,cl,"Canvas Current:",0,0)->Flags|=LA_TEXT_ALIGN_RIGHT; laShowItem(uil,cr,0,"our.canvas.color_interpretation");

    laShowSeparator(uil,c);
    laShowItem(uil,cl,Operator,"transparent")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_NO_CONFIRM;
    laShowSeparator(uil,c);

    b=laBeginRow(uil,c,0,0);laShowSeparator(uil,c)->Expand=1;laShowItem(uil,c,0,"LA_confirm")->Flags|=LA_UI_FLAGS_HIGHLIGHT;laEndRow(uil,b);
}

int ourinv_NewBrush(laOperator* a, laEvent* e){
    our_NewBrush("Our Brush",0,0.95,9,0.5,0.5,5,0,0,0,0);
    laNotifyUsers("our.tools.current_brush"); laNotifyUsers("our.tools.brushes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Add brush",0);
    return LA_FINISHED;
}
int ourinv_DuplicateBrush(laOperator* a, laEvent* e){
    OurBrush* b=a->This?a->This->EndInstance:0; if(!b) return LA_CANCELED;
    our_DuplicateBrush(b);
    laNotifyUsers("our.tools.current_brush"); laNotifyUsers("our.tools.brushes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Duplicate brush",0);
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
int ourinv_BrushQuickSwitch(laOperator* a, laEvent* e){
    char* id=strGetArgumentString(a->ExtraInstructionsP,"binding"); if(!id){ return LA_CANCELED; }
    int num; int ret=sscanf(id,"%d",&num); if(ret>9||ret<0){ return LA_CANCELED; }
    OurBrush* found=0,*first=0; int set=0;
    for(OurBrush* b=Our->Brushes.pFirst;b;b=b->Item.pNext){ 
        if(b->Binding==num){
            if(!first){ first=b; }
            if(found){ ourset_CurrentBrush(Our,b); set=1; laNotifyUsers("our.tools.brushes"); break; }
            elif(b == Our->CurrentBrush){ found = b; }
        }
    }
    if(!found || (found && !set)){ found = first; }
    if(!set && found){
         ourset_CurrentBrush(Our,found); laNotifyUsers("our.tools.brushes");
    }
    return LA_FINISHED;
}
int ourinv_BrushResize(laOperator* a, laEvent* e){
    OurBrush* b=Our->CurrentBrush; if(!b) return LA_CANCELED;
    char* direction=strGetArgumentString(a->ExtraInstructionsP,"direction");
    if(strSame(direction,"bigger")){ if(!Our->BrushNumber){ Our->BrushSize+=0.25; }else{ int num=Our->BrushNumber+1; TNS_CLAMP(num,1,10); Our->BrushNumber=num; Our->BrushSize=((real)Our->BrushNumber)/2; } }
    else{ if(!Our->BrushNumber){ Our->BrushSize-=0.25; }else{ int num=Our->BrushNumber-1; TNS_CLAMP(num,1,10); Our->BrushNumber=num; Our->BrushSize=((real)Our->BrushNumber)/2; } }
    TNS_CLAMP(Our->BrushSize,0,10); Our->ShowBrushNumber=1;
    laNotifyUsers("our.preferences.brush_size"); if(Our->BrushNumber){ laNotifyUsers("our.preferences.brush_number"); }
    return LA_FINISHED;
}
int ourinv_BrushSetNumber(laOperator* a, laEvent* e){
    OurBrush* b=Our->CurrentBrush; if(!b) return LA_CANCELED;
    char* number=strGetArgumentString(a->ExtraInstructionsP,"number"); if(!number){return LA_CANCELED;}
    switch(number[0]){
    case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
        Our->BrushNumber=number[0]-'0'+1; break;
    case '#': default:
        Our->BrushNumber=0;
    }
    laNotifyUsers("our.tools.current_brush.size"); laNotifyUsers("our.preferences.brush_number");
    return LA_FINISHED;
}

int ourinv_ToggleErase(laOperator* a, laEvent* e){
    OurBrush* b=Our->CurrentBrush; if(!b) return LA_FINISHED;
    if(Our->Erasing){ Our->Erasing=0; }else{ Our->Erasing=1; } laNotifyUsers("our.erasing");
    return LA_FINISHED;
}
int ourinv_CycleSketch(laOperator* a, laEvent* e){
    Our->SketchMode++; Our->SketchMode%=3;
    laNotifyUsers("our.canvas"); laNotifyUsers("our.canvas.sketch_mode");
    laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}

void ourset_BrushNumber(void* unused, int a);
void ourset_BrushSize(void* unused, real v);

int ourinv_AdjustBrush(laOperator* a, laEvent* e){
    OurCanvasDraw *ex = a->This?a->This->EndInstance:0; if(!ex) return LA_FINISHED;
    ex->CanvasDownX = e->x; ex->CanvasDownY=e->y; ex->LastSize=Our->BrushSize; ex->LastNumber=Our->BrushNumber;
    return LA_RUNNING;
}
int ourmod_AdjustBrushSize(laOperator* a, laEvent* e){
    OurCanvasDraw *ex = a->This?a->This->EndInstance:0; if(!ex) return LA_FINISHED;
    if((e->type&LA_MOUSE_EVENT)&&(e->type&LA_STATE_DOWN)){ return LA_FINISHED; }
    if(e->key == LA_KEY_ESCAPE || e->key==LA_KEY_ENTER){ return LA_FINISHED; }
    if(Our->BrushNumber!=0){
        real dist = e->x-ex->CanvasDownX+ex->CanvasDownY-e->y;
        int number = dist/LA_RH + ex->LastNumber;
        TNS_CLAMP(number,1,10);
        ourset_BrushNumber(0,number);
        laNotifyUsers("our.canvas_notify");
    }else{
        real dist = e->x-ex->CanvasDownX+ex->CanvasDownY-e->y;
        real newsize = dist/LA_RH + ex->LastSize;
        TNS_CLAMP(newsize,0.001,10);
        ourset_BrushSize(0,newsize);
        laNotifyUsers("our.canvas_notify");
    }
    return LA_RUNNING;
}

void our_SmoothGlobalInput(real *x, real *y, int reset){
    if(reset){ Our->LastX=*x; Our->LastY=*y; return; }
    else{
        real smfac=(1-Our->Smoothness/1.1);
        real xto=tnsLinearItp(Our->LastX,*x,smfac), yto=tnsLinearItp(Our->LastY,*y,smfac);
        *x=Our->LastX=xto; *y=Our->LastY=yto;
    }
}
int ourinv_Action(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    our_PaintResetBrushState(ob);
    real ofx,ofy; our_GetBrushOffset(ex,Our->CurrentBrush,e->Orientation,&ofx,&ofy); ex->DownTilt = e->Orientation;
    real x,y; our_UiToCanvas(&ex->Base,e,&x,&y); x-=ofx; y-=ofy; our_SmoothGlobalInput(&x,&y,1);
    ex->CanvasLastX=x;ex->CanvasLastY=y;ex->LastPressure=-1;ex->LastTilt[0]=e->Orientation;ex->LastTilt[1]=e->Deviation;
    ex->CanvasDownX=x; ex->CanvasDownY=y; ex->MovedX=0; ex->MovedY=0;
    Our->ActiveTool=Our->Tool; Our->CurrentScale = 1.0f/ex->Base.ZoomX;
    Our->xmin=FLT_MAX;Our->xmax=-FLT_MAX;Our->ymin=FLT_MAX;Our->ymax=-FLT_MAX; Our->ResetBrush=1; ex->HideBrushCircle=1;
    Our->PaintProcessedEvents=0; Our->BadEventsGiveUp=0; Our->BadEventCount=0;
    if(Our->ActiveTool==OUR_TOOL_CROP){ if(!Our->ShowBorder){ ex->HideBrushCircle=0; return LA_FINISHED;} our_StartCropping(ex); }
    if(l->Hide || l->Transparency==1 || l->Lock || (l->AsSketch && Our->SketchMode==2)){ ex->HideBrushCircle=0; return LA_FINISHED; }
    Our->LockBackground=1; laNotifyUsers("our.lock_background");
    our_EnsureEraser(e->IsEraser);
    laHideCursor();
    Our->ShowBrushName=0; Our->ShowBrushNumber=0;
    return LA_RUNNING;
}
int ourmod_Paint(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    if(e->type==LA_L_MOUSE_UP || e->type==LA_R_MOUSE_DOWN || (e->type == LA_KEY_DOWN && e->key==LA_KEY_ESCAPE)){
        if(Our->PaintProcessedEvents) our_RecordUndo(l,Our->xmin,Our->xmax,Our->ymin,Our->ymax,0,1);
        ex->HideBrushCircle=0; laShowCursor();
        laEvent* ue; while(ue=lstPopItem(&Our->BadEvents)){ memFree(ue); }
        return LA_FINISHED;
    }

    if(e->type==LA_MOUSEMOVE||e->type==LA_L_MOUSE_DOWN){
        if((!e->GoodPressure) && ((!Our->BadEventsGiveUp)||(!Our->AllowNonPressure))){
            laEvent* be=memAcquire(sizeof(laEvent)); memcpy(be,e,sizeof(laEvent)); be->Item.pNext=be->Item.pPrev=0;
            lstAppendItem(&Our->BadEvents,be); Our->BadEventCount++;
            if(Our->BadEventCount>=Our->BadEventsLimit){ Our->BadEventsGiveUp=1; }
        }else{
            Our->PaintProcessedEvents=1; laEvent* UseEvent;real Pressure=e->Pressure,Orientation=-e->Orientation,Deviation=e->Deviation,Twist=e->Twist;
            Pressure = pow(Pressure,Our->Hardness>=0?(Our->Hardness+1):(1+Our->Hardness/2));
            while(1){
                UseEvent=lstPopItem(&Our->BadEvents); if(!UseEvent){ UseEvent=e; }
                real ofx,ofy; our_GetBrushOffset(ex,Our->CurrentBrush,ex->DownTilt,&ofx,&ofy);
                real x,y; our_UiToCanvas(&ex->Base,UseEvent,&x,&y); x-=ofx; y-=ofy; our_SmoothGlobalInput(&x,&y,0);
                int tl,tr,tu,tb; if(ex->LastPressure<0){ ex->LastPressure=Pressure; }
                if(our_PaintGetDabs(ob,l,ex->CanvasLastX,ex->CanvasLastY,x,y,ex->LastPressure,ex->LastTilt[0],ex->LastTilt[1],ex->LastTwist,
                    Pressure,Orientation,Deviation,Twist,
                    &tl,&tr,&tu,&tb,&ex->CanvasLastX,&ex->CanvasLastY)){
                    our_PaintDoDabsWithSmudgeSegments(l,tl,tr,tu,tb);
                    laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
                }
                ex->LastPressure=Pressure;ex->LastTilt[0]=Orientation;ex->LastTilt[1]=Deviation; ex->LastTwist=Twist;
                if(UseEvent==e){ break; }
                else{ memFree(UseEvent); }
            }
        }
    }

    return LA_RUNNING;
}
int ourmod_Crop(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    if(e->type==LA_L_MOUSE_UP || e->type==LA_R_MOUSE_DOWN || (e->type == LA_KEY_DOWN && e->key==LA_KEY_ESCAPE)){  ex->HideBrushCircle=0; laShowCursor(); return LA_FINISHED; }

    if(e->type==LA_MOUSEMOVE||e->type==LA_L_MOUSE_DOWN){
        real x,y; our_UiToCanvas(&ex->Base,e,&x,&y);
        our_DoCropping(ex,x,y);
        laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    }

    return LA_RUNNING;
}
int ourmod_Move(laOperator* a, laEvent* e){
    OurLayer* l=Our->CurrentLayer; OurCanvasDraw *ex = a->This?a->This->EndInstance:0; OurBrush* ob=Our->CurrentBrush; if(!l||!ex||!ob) return LA_CANCELED;
    if(e->type==LA_L_MOUSE_UP || e->type==LA_R_MOUSE_DOWN || (e->type == LA_KEY_DOWN && e->key==LA_KEY_ESCAPE)){
        OurMoveUndo* undo = memAcquire(sizeof(OurMoveUndo));
        undo->dx = ex->MovedX; undo->dy = ex->MovedY; undo->Layer = Our->CurrentLayer;
        laFreeNewerDifferences();
        laRecordCustomDifferences(undo,ourundo_Move,ourredo_Move,memFree); laPushDifferences("Move layer",0);
        ex->HideBrushCircle=0; laShowCursor(); return LA_FINISHED;
    }

    if(e->type==LA_MOUSEMOVE||e->type==LA_L_MOUSE_DOWN){
        real x,y; our_UiToCanvas(&ex->Base,e,&x,&y);
        int movedx=0,movedy=0;
        our_DoMoving(ex,x,y,&movedx,&movedy);
        ex->MovedX+=movedx; ex->MovedY+=movedy;
        laNotifyUsers("our.canvas"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
    }

    return LA_RUNNING;
}
int ourmod_Action(laOperator* a, laEvent* e){
    OurCanvasDraw *ex = a->This?a->This->EndInstance:0; if(!ex) return LA_CANCELED;
    OurLayer* l=Our->CurrentLayer; OurBrush* ob=Our->CurrentBrush;
    switch(Our->ActiveTool){
    case OUR_TOOL_PAINT: if(!l||!ob) return LA_CANCELED;
        return ourmod_Paint(a,e);
    case OUR_TOOL_CROP:
        return ourmod_Crop(a,e);
    case OUR_TOOL_MOVE:
        return ourmod_Move(a,e);
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

    if(e->type==LA_R_MOUSE_UP || e->type==LA_L_MOUSE_UP || (e->type == LA_KEY_DOWN && e->key==LA_KEY_ESCAPE)){  ex->HideBrushCircle=0; return LA_FINISHED; }

    if(e->type==LA_MOUSEMOVE||e->type==LA_R_MOUSE_DOWN){
        our_ReadWidgetColor(ex, e->x-ui->L, ui->B-e->y); laNotifyUsers("our.current_color");
    }

    return LA_RUNNING;
}
int ourchk_CropToRef(laPropPack *This, laStringSplitor *ss){ if(Our->ShowRef&&Our->ShowBorder) return 1; return 0; }
int ourinv_CropToRef(laOperator* a, laEvent* e){
    if((!Our->ShowRef) || (!Our->ShowBorder)) return LA_FINISHED;
    real W,H,W2,H2; OUR_GET_REF_SIZE(W,H)
    char* arg = strGetArgumentString(a->ExtraInstructionsP,"border");
    if(strSame(arg,"outer")){
        W+=Our->RefPaddings[0]*2; H+=Our->RefPaddings[1]*2;
    }elif(strSame(arg,"inner")){
        W-=Our->RefMargins[0]*2; H-=Our->RefMargins[1]*2;
    }
    real dpc=OUR_DPC; W*=dpc; H*=dpc; W2=W/2; H2=H/2;
    Our->X=-W2; Our->W=W; Our->Y=H2; Our->H=H;
    if(Our->ShowRef==2){
        if(Our->RefCutHalf==1){
            if(Our->RefOrientation){ Our->H=H2; }else{ Our->W=W2; }
        }elif(Our->RefCutHalf==2){
            if(Our->RefOrientation){ Our->H-=H2;Our->Y+=H2; }else{ Our->W=W2; Our->X+=W2;  }
        }
    }
    laMarkMemChanged(Our->CanvasSaverDummyList.pFirst); laNotifyUsers("our.canvas");
    return LA_FINISHED;
}

OurColorPallette* our_NewPallette(char* Name){
    OurColorPallette* cp=memAcquireHyper(sizeof(OurColorPallette));
    strSafeSet(&cp->Name,Name); lstAppendItem(&Our->Pallettes,cp); memAssignRef(Our,&Our->CurrentPallette,cp);
    return cp;
}
OurColorItem* our_PalletteNewColor(OurColorPallette* cp,tnsVector3d Color){
    OurColorItem* ci=memAcquire(sizeof(OurColorItem)); memAssignRef(ci,&ci->Parent,cp);
    tnsVectorSet3v(ci->Color,Color); lstAppendItem(&cp->Colors,ci); return ci;
    laMarkMemChanged(cp);
}
void our_PalletteRemoveColor(OurColorItem* ci){
    lstRemoveItem(&ci->Parent->Colors,ci); memLeave(ci); laMarkMemChanged(ci->Parent);
}
void our_RemovePallette(OurColorPallette* cp){
    strSafeDestroy(&cp->Name); while(cp->Colors.pFirst){ our_PalletteRemoveColor(cp->Colors.pFirst); }
    if(Our->CurrentPallette==cp){
        if(cp->Item.pNext){ memAssignRef(Our,&Our->CurrentPallette,cp->Item.pNext); }
        else { memAssignRef(Our,&Our->CurrentPallette,cp->Item.pPrev); }
    }
    lstRemoveItem(&Our->Pallettes,cp); memLeave(cp);
}

int ourinv_NewPallette(laOperator* a, laEvent* e){
    our_NewPallette("Our Pallette");
    laNotifyUsers("our.tools.current_pallette"); laNotifyUsers("our.tools.pallettes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Add pallette",0);
    return LA_FINISHED;
}
int ourinv_RemovePallette(laOperator* a, laEvent* e){
    OurColorPallette* cp=Our->CurrentPallette; if(a->This && a->This->EndInstance){ cp=a->This->EndInstance; }
    if(!cp) return LA_FINISHED;
    our_RemovePallette(cp);
    laNotifyUsers("our.tools.current_pallette"); laNotifyUsers("our.tools.pallettes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Remove pallette",0);
    return LA_FINISHED;
}
int ourinv_PalletteNewColor(laOperator* a, laEvent* e){
    OurColorPallette* cp=Our->CurrentPallette; if(a->This && a->This->EndInstance){ cp=a->This->EndInstance; }
    if(!cp) return LA_FINISHED;
    our_PalletteNewColor(cp,Our->CurrentColor);
    laNotifyUsers("our.tools.current_pallette"); laNotifyUsers("our.tools.pallettes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Add color",0);
    return LA_FINISHED;
}
int ourinv_PalletteRemoveColor(laOperator* a, laEvent* e){
    OurColorItem* ci=0; if(a->This && a->This->EndInstance){ ci=a->This->EndInstance; }
    if(!ci) return LA_FINISHED;
    our_PalletteRemoveColor(ci);
    laNotifyUsers("our.tools.current_pallette"); laNotifyUsers("our.tools.pallettes"); laRecordInstanceDifferences(Our,"our_tools"); laPushDifferences("Remove pallette",0);
    return LA_FINISHED;
}

int our_TileHasPixels(OurTexTile* ot){
    if(!ot || !ot->Texture) return 0;
    int bufsize=OUR_TILE_W*OUR_TILE_W*OUR_CANVAS_PIXEL_SIZE;
    ot->Data=malloc(bufsize); int width=OUR_TILE_W;
    tnsBindTexture(ot->Texture); glPixelStorei(GL_PACK_ALIGNMENT, 1);
    tnsGet2DTextureSubImage(ot->Texture, 0, 0, width, width, OUR_CANVAS_GL_FORMAT, OUR_CANVAS_DATA_FORMAT, bufsize, ot->Data);
    
    int has=0;
    int total_elems = width*width;
    for(int i=0;i<total_elems;i++){
        if(ot->Data[i*4+3]!=0){ has=1; break; }
    }
    free(ot->Data); ot->Data=0;
    return has;
}
void our_LayerClearEmptyTiles(OurLayer* ol){
    for(int row=0;row<OUR_TILES_PER_ROW;row++){ if(!ol->TexTiles[row]) continue;
        int rowhas=0;
        for(int col=0;col<OUR_TILES_PER_ROW;col++){ if(!ol->TexTiles[row][col]) continue;
            OurTexTile* ot=ol->TexTiles[row][col];
            if(!our_TileHasPixels(ot)){
                if(ot->Texture){ tnsDeleteTexture(ot->Texture); ot->Texture=0; }
                if(ot->Data){ free(ot->Data); ot->Data=0; }
                if(ot->FullData){ free(ot->FullData); ot->FullData=0; }
                if(ot->CopyBuffer){ free(ot->CopyBuffer); ot->CopyBuffer=0; }
                memFree(ot); ol->TexTiles[row][col]=0;
            }else{
                rowhas=1;
            }
        }
        if(!rowhas){
            memFree(ol->TexTiles[row]); ol->TexTiles[row]=0;
        }

    }
}
int ourinv_ClearEmptyTiles(laOperator* a, laEvent* e){
    for(OurLayer* ol=Our->Layers.pFirst;ol;ol=ol->Item.pNext){
        our_LayerClearEmptyTiles(ol);
    }
    laNotifyUsers("our.canvas_notify");
    return LA_FINISHED;
}

int ourgetstate_Canvas(void* unused_canvas){
    int level; laMemNodeHyper* m=memGetHead(Our->CanvasSaverDummyList.pFirst,&level); if(!m || level!=2) return -1;
    if(m->Modified) return LA_BT_WARNING;
    return -1;
}
int ourgetstate_Brush(OurBrush* brush){
    int level; laMemNodeHyper* m=memGetHead(brush,&level); if(!m || level!=2) return -1;
    if(m->Modified || !m->FromFile) return LA_BT_WARNING;
    return -1;
}
int ourgetstate_Pallette(OurColorPallette* pallette){
    int level; laMemNodeHyper* m=memGetHead(pallette,&level); if(!m || level!=2) return -1;
    if(m->Modified || !m->FromFile) return LA_BT_WARNING;
    return -1;
}
void* ourgetraw_FileThumbnail(void* unused, uint32_t* r_size, int* r_is_copy){
    void* buf=0;
    if(our_RenderThumbnail(&buf, r_size)){ *r_is_copy=1; return buf; }
    *r_is_copy=0; return 0;
}
void oursetraw_FileThumbnail(void* unused, void* data, int DataSize){
    return;
}
void ourget_CanvasIdentifier(void* unused, char* buf, char** ptr){
    *ptr=transLate("Main canvas");
}
void* ourget_FirstLayer(void* unused, void* unused1){
    return Our->Layers.pFirst;
}
void* ourget_FirstBrush(void* unused, void* unused1){
    return Our->Brushes.pFirst;
}
void* ourget_FirstPallette(void* unused, void* unused1){
    return Our->Pallettes.pFirst;
}
void* ourget_our(void* unused, void* unused1){
    return Our;
}
void ourget_LayerTileStart(OurLayer* l, int* xy){
    our_LayerClearEmptyTiles(l);
    our_LayerEnsureImageBuffer(l, 1); xy[0]=Our->ImageX; xy[1]=Our->ImageY+Our->ImageH;
}
void ourset_LayerTileStart(OurLayer* l, int* xy){
    Our->TempLoadX = xy[0]; Our->TempLoadY = xy[1];
}
void* ourget_LayerImage(OurLayer* l, uint32_t* r_size, int* r_is_copy){
    static int LayerCount=0; static int CurrentLayer=0;
    void* buf=0; if(!l->Item.pPrev){ LayerCount=lstCountElements(&Our->Layers); CurrentLayer=0; }
    CurrentLayer++; laShowProgress((real)CurrentLayer/LayerCount,-1);
    our_LayerClearEmptyTiles(l);
    int ensure=our_LayerEnsureImageBuffer(l, 0);
    if(ensure<=0){ if(!ensure){ our_ShowAllocationError(0); } *r_is_copy=0; return 0; }
    our_LayerToImageBuffer(l, 0);
    our_ImageBufferFromNative();
    if(our_ImageExportPNG(0,1,&buf,r_size, 0, OUR_EXPORT_BIT_DEPTH_16, OUR_EXPORT_COLOR_MODE_FLAT,0,0)){ *r_is_copy=1; return buf; }
    *r_is_copy=0; return buf;
}
OurThreadImportPNGData* ourthread_ImportPNGGetTask(OurThreadImportPNGDataMain* main){
    laSpinLock(&main->lock);
    if(main->next>=main->max){ laSpinUnlock(&main->lock); return 0; }
    OurThreadImportPNGData* d=&main->data[main->next]; main->next++;
    laSpinUnlock(&main->lock);
    return d;
}
int ourthread_ImportPNG(OurThreadImportPNGDataMain* main){
    OurThreadImportPNGData* data;
    while(data=ourthread_ImportPNGGetTask(main)){
        our_LayerImportPNG(data->l, 0, data->data, 0, 0, 1, Our->TempLoadX, data->starty,1);
    }
}
static int our_ProcessorCount() {
#ifdef __linux__
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
#ifdef _WIN32
    SYSTEM_INFO sysinfo; GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#endif
    return 1;
}
void ourset_LayerImage(OurLayer* l, void* pdata, uint32_t size){
    if(!pdata) return; char* data=pdata;
    if(l->ReadSegmented.Count>0){
        OurLayerImageSegmented* seg=data; data+=sizeof(OurLayerImageSegmented);

        logPrint("\n    Reading segmented layer for size %dx%d...",seg->Width,seg->Height);

        int threads = our_ProcessorCount(); TNS_CLAMP(threads,1,32);
        int taskcount=l->ReadSegmented.Count;
        if(threads>taskcount){threads=taskcount;}
        thrd_t* th=calloc(threads,sizeof(thrd_t));
        OurThreadImportPNGData* edata=calloc(taskcount,sizeof(OurThreadImportPNGData));
        OurThreadImportPNGDataMain emain={0};
        emain.data=edata;emain.max=taskcount;emain.next=0; laSpinInit(&emain.lock);

        LA_ACQUIRE_GLES_CONTEXT;

        int StartY=Our->TempLoadY; uint64_t offset=0;
        our_EnsureImageBufferOnRead(l,seg->Width,seg->Height,1,Our->TempLoadX,Our->TempLoadY);
        int LoadY=Our->ImageH;

        LA_LEAVE_GLES_CONTEXT;

        for(int i=0;i<taskcount;i++){
            LoadY-=l->ReadSegmented.H[i];
            edata[i].main=&emain; edata[i].starty=LoadY; edata[i].data=&data[offset]; edata[i].l=l;
            offset+=seg->Sizes[i]; 
        }
        for(int i=0;i<threads;i++){ thrd_create(&th[i],ourthread_ImportPNG,&emain); }
        for(int i=0;i<threads;i++){ int result = thrd_join(th[i], NULL); }

        laSpinDestroy(&emain.lock);
        free(th); free(edata);

        LA_ACQUIRE_GLES_CONTEXT;

        our_ImageBufferToNative();
        our_LayerToTexture(l); if(Our->ImageBuffer){ free(Our->ImageBuffer); Our->ImageBuffer=0; }

        LA_LEAVE_GLES_CONTEXT;
        return;
    }
    LA_ACQUIRE_GLES_CONTEXT;
    our_LayerImportPNG(l, 0, data, 0, 0, 1, Our->TempLoadX, Our->TempLoadY,0);
    LA_LEAVE_GLES_CONTEXT;
}
int ourget_LayerImageShouldSegment(OurLayer* unused){
    return Our->SegmentedWrite;
}
void writetestpngfiles(void* data, int size, int i){
    char buf[128]; sprintf(buf,"p%d.png",i);
    FILE* f=fopen(buf,"wb"); fwrite(data,size,1,f); fclose(f);
}
int ourthread_ExportPNG(OurThreadExportPNGData* data){
    if(!our_ImageExportPNG(0,1,&data->pointers[data->i+1],&data->r_sizes[data->i+1], 0, OUR_EXPORT_BIT_DEPTH_16, OUR_EXPORT_COLOR_MODE_FLAT,data->segy,data->h)){ data->fail=1; }
}
void ourget_LayerImageSegmented(OurLayer* l, int* r_chunks, uint32_t* r_sizes, void** pointers){
    static int LayerCount=0; static int CurrentLayer=0;
    void* buf=0; if(!l->Item.pPrev){ LayerCount=lstCountElements(&Our->Layers); CurrentLayer=0; }
    CurrentLayer++; laShowProgress((real)CurrentLayer/LayerCount,-1);
    our_LayerClearEmptyTiles(l);
    int ensure=our_LayerEnsureImageBuffer(l, 0);
    if(ensure<=0){ if(!ensure){ our_ShowAllocationError(0); } *r_chunks=0; return; }
    our_LayerToImageBuffer(l, 0);
    our_ImageBufferFromNative();

    OurLayerImageSegmented* seg=calloc(1,sizeof(OurLayerImageSegmented));
    memcpy(seg, &l->ReadSegmented,sizeof(OurLayerImageSegmented));
    int threads=seg->Count; *r_chunks=seg->Count+1;
    pointers[0]=seg; r_sizes[0]=sizeof(OurLayerImageSegmented);

    logPrintNew("\n    Writing segmented layer...");

    LA_LEAVE_GLES_CONTEXT;

    int segy=0;  int anyfailed=0;
    thrd_t* th=calloc(threads,sizeof(thrd_t));
    OurThreadExportPNGData* edata=calloc(threads,sizeof(OurThreadExportPNGData));
    for(int i=0;i<threads;i++){
        edata[i].i=i; edata[i].pointers=pointers;edata[i].r_sizes=r_sizes;edata[i].h=seg->H[i];edata[i].segy=segy;
        thrd_create(&th[i],ourthread_ExportPNG,&edata[i]);
        segy+=seg->H[i];
    }
    for(int i=0;i<threads;i++){ int result = thrd_join(th[i], NULL); }
    for(int i=0;i<threads;i++){ seg->Sizes[i]=r_sizes[i+1]; anyfailed+=edata[i].fail; }
    free(th); free(edata);

    LA_ACQUIRE_GLES_CONTEXT;

    if(Our->ImageBuffer){ free(Our->ImageBuffer); Our->ImageBuffer=0; }
    if(anyfailed){ *r_chunks=0;
        for(int i=0;i<threads;i++){ if(pointers[i]){ free(pointers[i]); pointers[i]=0; } }
        logPrintNew("    [ ERROR ] Failed to write some segments of the layer (%dx%d). Nothing written as a result.\n",Our->ImageW,Our->ImageH);
        return;
    }
    logPrint(" for size %dx%d",Our->ImageW,segy);
}
void* ourget_LayerImageSegmentedInfo(OurLayer* l, int* r_size, int* r_is_copy){
    if(!Our->SegmentedWrite){ *r_is_copy=0; *r_size=0; return 0; }

    int threads = our_ProcessorCount(); TNS_CLAMP(threads, 1, 32);
    int X,Y,W,H; our_GetFinalDimension(0,0,0,&X,&Y,&W,&H); l->ReadSegmented.Width=W; l->ReadSegmented.Height=H;
    int useh=H/threads; l->ReadSegmented.Count=threads; 
    for(int i=0;i<threads-1;i++){ l->ReadSegmented.H[i]=useh; } l->ReadSegmented.H[threads-1]=H-useh*(threads-1);

    *r_is_copy=0; *r_size=sizeof(OurLayerImageSegmented); return &l->ReadSegmented;
}
void ourset_LayerImageSegmentedInfo(OurLayer* l, void* data, int size){
    if (data && size) {
        memcpy(&l->ReadSegmented, data, sizeof(OurLayerImageSegmented));
    }
}

void ourset_LayerMove(OurLayer* l, int move){
    if(move<0 && l->Item.pPrev){ lstMoveUp(&Our->Layers, l); laNotifyUsers("our.canvas_notify"); }
    elif(move>0 && l->Item.pNext){ lstMoveDown(&Our->Layers, l); laNotifyUsers("our.canvas_notify"); }
}
void ourset_LayerAlpha(OurLayer* l, real a){
    l->Transparency=a; laNotifyUsers("our.canvas_notify");  laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_LayerHide(OurLayer* l, int hide){
    l->Hide=hide; laNotifyUsers("our.canvas_notify");  laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_LayerAsSketch(OurLayer* l, int sketch){
    l->AsSketch=sketch; laNotifyUsers("our.canvas_notify");  laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_LayerBlendMode(OurLayer* l, int mode){
    l->BlendMode=mode; laNotifyUsers("our.canvas_notify");  laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_BrushMove(OurBrush* b, int move){
    if(move<0 && b->Item.pPrev){ lstMoveUp(&Our->Brushes, b); laNotifyUsers("our.tools.brushes"); }
    elif(move>0 && b->Item.pNext){ lstMoveDown(&Our->Brushes, b); laNotifyUsers("our.tools.brushes"); }
}
void ourset_BrushSize(void* unused, real v){
    Our->BrushSize = v; Our->ShowBrushNumber=1; laNotifyUsers("our.canvas_notify");
}
void ourset_BrushBaseSize(void* unused, real v){
    Our->BrushBaseSize = v; Our->ShowBrushNumber=1; laNotifyUsers("our.canvas_notify");
}
void ourset_BackgroundColor(void* unused, real* arr){
    memcpy(Our->BackgroundColor, arr, sizeof(real)*3); laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_BorderAlpha(void* unused, real a){
    Our->BorderAlpha=a; laNotifyUsers("our.canvas_notify");  laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_Tool(void* unused, int a){
    Our->Tool=a; laNotifyUsers("our.canvas_notify");
}
void ourset_BorderFadeWidth(void* unused, real a){
    Our->BorderFadeWidth=a; laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_ShowBorder(void* unused, int a){
    Our->ShowBorder=a; laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_ColorInterpretation(void* unused, int a){
    Our->ColorInterpretation=a; laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_ShowTiles(void* unused, int a){ Our->ShowTiles=a; laNotifyUsers("our.canvas_notify"); }
void ourset_ShowStripes(void* unused, int a){ Our->ShowStripes=a; laNotifyUsers("our.canvas_notify"); }
void ourset_ShowGrid(void* unused, int a){ Our->ShowGrid=a; laNotifyUsers("our.canvas_notify"); }
void ourset_CanvasSize(void* unused, int* wh){
    Our->W=wh[0]; Our->H=wh[1]; if(Our->W<32) Our->W=32; if(Our->H<32) Our->H=32; laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_CanvasPosition(void* unused, int* xy){
    Our->X=xy[0]; Our->Y=xy[1]; laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_LayerPosition(OurLayer* l, int* xy){
    l->OffsetX=xy[0]; l->OffsetY=xy[1]; laNotifyUsers("our.canvas_notify"); laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourreset_Canvas(OurPaint* op){
    while(op->Layers.pFirst){ our_RemoveLayer(op->Layers.pFirst,1); }
}
void ourreset_Preferences(OurPaint* op){
    return; //does nothing.
}
void ourpropagate_Tools(OurPaint* p, laUDF* udf, int force){
    for(OurBrush* b=p->Brushes.pFirst;b;b=b->Item.pNext){
        if(force || !laget_InstanceActiveUDF(b)){ laset_InstanceUDF(b, udf); }
    }
    for(OurColorPallette* cp=p->Pallettes.pFirst;cp;cp=cp->Item.pNext){
        if(force || !laget_InstanceActiveUDF(cp)){ laset_InstanceUDF(cp, udf); }
    }
}
void ourset_CurrentBrush(void* unused, OurBrush* b){
    real r;
    OurBrush* ob=Our->CurrentBrush;
    if(ob){
        if(ob->DefaultAsEraser){ Our->SaveEraserSize=Our->BrushSize; }else{ Our->SaveBrushSize=Our->BrushSize; }
    }
    Our->CurrentBrush=b; if(b){
        if(b->DefaultAsEraser){ Our->Erasing=1; Our->EraserID=b->Binding; if(Our->SaveEraserSize)Our->BrushSize=Our->SaveEraserSize; }
        else{ Our->Erasing=0; Our->PenID=b->Binding; if(Our->SaveBrushSize)Our->BrushSize=Our->SaveBrushSize; }
    }
    Our->ShowBrushName = 1; Our->ShowBrushNumber=1;
    laNotifyUsers("our.tools.current_brush"); laNotifyUsers("our.erasing"); laGraphRequestRebuild();
}
void ourset_CurrentLayer(void* unused, OurLayer*l){
    memAssignRef(Our, &Our->CurrentLayer, l); laNotifyUsers("our.canvas_notify");
}
void ourset_CurrentPallette(void* unused, OurColorPallette* cp){
    memAssignRef(Our,&Our->CurrentPallette,cp);
    laNotifyUsers("our.tools.current_pallette"); laNotifyUsers("our.tools.pallettes");
}
void ourset_PalletteColor(void* unused, OurColorItem* ci){
    tnsVectorSet3v(Our->CurrentColor,ci->Color);
    laNotifyUsers("our.current_color");
}
float ourget_ColorBoost(void* unused){
    return tnsLength3d(Our->CurrentColor);
}
void ourset_ColorBoost(void* unused, real boost){
    real v=tnsLength3d(Our->CurrentColor);
    if(v<=0){ tnsVectorSet3(Our->CurrentColor,boost,boost,boost); return; }
    real fac = boost/v;
    tnsVectorMultiSelf3d(Our->CurrentColor,fac);
    laNotifyUsers("our.color_boost");
}
void ourset_ShowRef(void* unused, int c){ Our->ShowRef=c; laNotifyUsers("our.canvas_notify"); }
void ourset_RefCategory(void* unused, int c){ Our->RefCategory=c; laNotifyUsers("our.canvas_notify"); }
void ourset_RefSize(void* unused, int c){ Our->RefSize=c; laNotifyUsers("our.canvas_notify"); }
void ourset_RefOrientation(void* unused, int c){ Our->RefOrientation=c; laNotifyUsers("our.canvas_notify"); }
void ourset_RefMargins(void* unused, real* v){ tnsVectorSet2v(Our->RefMargins,v);laNotifyUsers("our.canvas_notify"); }
void ourset_RefPaddings(void* unused, real* v){ tnsVectorSet2v(Our->RefPaddings,v); laNotifyUsers("our.canvas_notify"); }
void ourset_RefMiddleMargin(void* unused, real v){ Our->RefMargins[2]=v;laNotifyUsers("our.canvas_notify"); }
void ourset_RefAlpha(void* unused, real a){
    Our->RefAlpha=a; laNotifyUsers("our.canvas_notify");  laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
}
void ourset_BrushPage(void* unused, int a){ Our->BrushPage=a; laNotifyUsers("our.tools.brushes"); }
void ourset_BrushNumber(void* unused, int a){ TNS_CLAMP(a,0,10); Our->BrushNumber=a;
    if(Our->CurrentBrush && a!=0){
        Our->BrushSize = ((real)a)/2;
        laNotifyUsers("our.tools.current_brush.size");
        laNotifyUsers("our.tools.brushes");
    }
}
void ourset_BrushShowInPages(OurBrush* b, int index, int v){
    int flag=(1<<index); if(v){ b->ShowInPages|=flag; }else{ b->ShowInPages&=(~flag); }
    laNotifyUsers("our.tools.brushes");
}
void ourget_BrushShowInPages(OurBrush* b, int* v){
    v[0]=(b->ShowInPages&(1<<0))?1:0;
    v[1]=(b->ShowInPages&(1<<1))?1:0;
    v[2]=(b->ShowInPages&(1<<2))?1:0;
}
int ourfilter_BrushInPage(void* Unused, OurBrush* b){
    if((!Our->BrushPage) || Our->BrushPage==OUR_BRUSH_PAGE_LIST) return 1;
    if(Our->BrushPage==1 && (b->ShowInPages&(1<<0))) return 1;
    if(Our->BrushPage==2 && (b->ShowInPages&(1<<1))) return 1;
    if(Our->BrushPage==3 && (b->ShowInPages&(1<<2))) return 1;
    return 0;
}
void ourset_ShowSketch(void* unused, int c){ Our->SketchMode=c; laNotifyUsers("our.canvas_notify"); }

int ourget_CanvasVersion(void* unused){
    return OUR_VERSION_MAJOR*100+OUR_VERSION_MINOR*10+OUR_VERSION_SUB;
}
void ourpost_Canvas(void* unused){
    if(Our->CanvasVersion<20){ Our->BackgroundFactor=0; Our->BackgroundType=0; }
    LA_ACQUIRE_GLES_CONTEXT;
    laMarkMemClean(Our->CanvasSaverDummyList.pFirst);
}

#define OUR_ADD_PRESSURE_SWITCH(p) \
    laAddEnumItemAs(p,"NONE","None","Not using pressure",0,0);\
    laAddEnumItemAs(p,"ENABLED","Enabled","Using pressure",1,0);

void ourui_MenuButtons(laUiList *uil, laPropPack *pp, laPropPack *actinst, laColumn *extracol, int context){
    laUiList *muil; laColumn *mc,*c = laFirstColumn(uil);
    muil = laMakeMenuPage(uil, c, "File");{
        mc = laFirstColumn(muil);
        laShowLabel(muil, mc, "Our Paint", 0, 0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
        laShowItem(muil, mc, 0, "LA_udf_read");
        laShowItemFull(muil, mc, 0, "LA_udf_read",0,"mode=append;text=Append",0,0);
        laShowSeparator(muil,mc);
        laShowItemFull(muil, mc, 0, "LA_managed_save",0,"quiet=true;text=Save;",0,0);
        laShowItem(muil, mc, 0, "LA_managed_save");
        laShowLabel(muil, mc, "Image", 0, 0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
        laShowItem(muil, mc, 0, "OUR_export_image");
        laShowLabel(muil, mc, "Layer", 0, 0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
        laShowItem(muil, mc, 0, "OUR_import_layer");
        laShowItem(muil, mc, 0, "OUR_export_layer");
        laShowLabel(muil, mc, "Others", 0, 0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
        laShowItem(muil, mc, 0, "LA_terminate_program");
        //laui_DefaultMenuButtonsFileEntries(muil,pp,actinst,extracol,0);
    }
    muil = laMakeMenuPage(uil, c, "Edit");{
        mc = laFirstColumn(muil); laui_DefaultMenuButtonsEditEntries(muil,pp,actinst,extracol,0);
        laShowSeparator(muil,mc);
        laShowLabel(muil,mc,"Canvas",0,0)->Flags|=LA_UI_FLAGS_DISABLED;
        laUiItem* row=laBeginRow(muil,mc,0,0);
        laShowItem(muil,mc,0,"OUR_clear_empty_tiles");
        laShowItemFull(muil,mc,0,"our.preferences.show_debug_tiles",LA_WIDGET_ENUM_HIGHLIGHT,"text=👁",0,0);
        laEndRow(muil,row);
    }
    muil = laMakeMenuPage(uil, c, "Options"); {
        mc = laFirstColumn(muil);
        laShowLabel(muil, mc, "Settings", 0, 0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
        laShowItemFull(muil, mc, 0, "LA_panel_activator", 0, "panel_id=LAUI_user_preferences;", 0, 0);
        
        laShowLabel(muil, mc, "Help", 0, 0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
        laShowItemFull(muil, mc, 0, "LA_open_internet_link", 0, "icon=📖;link=http://www.ChengduLittleA.com/ourpaintmanual;text=User Manual", 0, 0);
        laShowItemFull(muil, mc, 0, "LA_open_internet_link", 0, "icon=★;link=https://www.wellobserve.com/index.php?post=20240421171033;text=Release Notes", 0, 0);
        laShowItemFull(muil, mc, 0, "LA_open_internet_link", 0, "icon=🐞;link=https://www.wellobserve.com/repositories/chengdulittlea/OurPaint/issues;text=Report a Bug", 0, 0);
        
        laShowLabel(muil, mc, "Information", 0, 0)->Flags|=LA_TEXT_MONO|LA_UI_FLAGS_DISABLED;
        laShowItemFull(muil, mc, 0, "LA_panel_activator", 0, "panel_id=LAUI_about;text=About;", 0, 0);

#ifdef _WIN32
        laShowSeparator(muil,mc);
        laShowItem(muil,mc,0,"LA_toggle_system_console");
#endif
        if(MAIN.InitArgs.HasTerminal){
#ifndef _WIN32
            laShowSeparator(muil,mc);
#endif
            laShowItemFull(muil, mc, 0, "LA_panel_activator", 0, "panel_id=LAUI_terminal;", 0, 0);
        }
    }
}
void ourui_ToolExtras(laUiList *uil, laPropPack *pp, laPropPack *actinst, laColumn *extracol, int context){
    laColumn *c = laFirstColumn(uil);
    laUiItem* b1,*b2;
    b1=laOnConditionThat(uil,c,laPropExpression(0,"our.preferences.undo_on_header"));{
        laShowItem(uil, c, 0, "LA_undo")->Flags|=LA_UI_FLAGS_NO_CONFIRM|LA_UI_FLAGS_ICON;
        laShowItem(uil, c, 0, "LA_redo")->Flags|=LA_UI_FLAGS_NO_CONFIRM|LA_UI_FLAGS_ICON;
    }laEndCondition(uil,b1);
    b1=laOnConditionThat(uil,c,laPropExpression(0,"our.preferences.tools_on_header"));{
        laShowItemFull(uil,c,0,"our.tool",0,0,0,0)->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_ICON;
    }laEndCondition(uil,b1);

    laUiItem* b=laOnConditionThat(uil,c,laEqual(laPropExpression(0,"our.tool"),laIntExpression(0)));{
        b1=laOnConditionThat(uil,c,laPropExpression(0,"our.preferences.mix_mode_on_header"));{
            laShowItemFull(uil,c,0,"our.erasing",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0)->Flags|=LA_UI_FLAGS_NO_CONFIRM;
            b2=laOnConditionThat(uil,c,laPropExpression(0,"our.erasing"));{
                laShowItem(uil,c,0,"our.brush_mix")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_ICON|LA_UI_FLAGS_DISABLED|LA_UI_FLAGS_NO_CONFIRM;
            }laElse(uil,b2);{
                laShowItem(uil,c,0,"our.brush_mix")->Flags|=LA_UI_FLAGS_EXPAND|LA_UI_FLAGS_ICON|LA_UI_FLAGS_NO_CONFIRM;
            }laEndCondition(uil,b2);
        }laEndCondition(uil,b1);

        b1=laOnConditionThat(uil,c,laPropExpression(0,"our.preferences.brush_numbers_on_header"));{
            laShowItem(uil,c,0,"our.preferences.brush_number")->Flags|=LA_UI_FLAGS_EXPAND;
        }laEndCondition(uil,b1);
    }laEndCondition(uil,b);
    char str[100]; sprintf(str,"text=%s",MAIN.MenuProgramName);
    laShowItemFull(uil,c,0,"OUR_show_splash",0,str,0,0)->Flags|=LA_UI_FLAGS_NO_DECAL|LA_UI_FLAGS_NO_TOOLTIP|LA_UI_FLAGS_EXIT_WHEN_TRIGGERED;
#ifdef LAGUI_ANDROID
    laUiList* mu;
    mu = laMakeMenuPage(uil,c,"🖌"); ourui_BrushesPanel(mu,0,0,0,0);
    mu = laMakeMenuPageEx(uil,c,"🎨",LA_UI_FLAGS_MENU_FLOAT16); ourui_ColorPanel(mu,0,0,0,0);
    mu = laMakeMenuPage(uil,c,"🔧"); ourui_ToolsPanel(mu,0,0,0,0);
    mu = laMakeMenuPage(uil,c,"☰"); ourui_LayersPanel(mu,0,0,0,0);
#endif
    laShowSeparator(uil,c)->Expand=1;
}

int our_FileAssociationsRegistered(){
#ifdef __linux__
    char* homedir=getenv("HOME"); char buf[2048]; struct stat statbuf;
    sprintf(buf,"%s/.local/share/mime/image/ourpaint.xml",homedir);
    if(stat(buf, &statbuf) != 0 || (S_ISDIR(statbuf.st_mode))){ return 0; }
    sprintf(buf,"%s/.local/share/thumbnailers/ourpaint.thumbnailer",homedir);
    if(stat(buf, &statbuf) != 0 || (S_ISDIR(statbuf.st_mode))){ return 0; }
    sprintf(buf,"%s/.local/share/applications/ourpaint.desktop",homedir);
    if(stat(buf, &statbuf) != 0 || (S_ISDIR(statbuf.st_mode))){ return 0; }
    return 1;
#endif
#ifdef _WIN32
    return 0;
#endif
}
int our_RegisterFileAssociations(){
#ifdef __linux__
    char* homedir=getenv("HOME"); char buf[2048]; char exepath[1024]; int failed=0; FILE* f;
    logPrintNew("Registering file associations...\n");

    int exepathsize=readlink("/proc/self/exe",exepath,1024);
    if(exepathsize<0){ logPrint("Unknown executable path\n",buf); failed=1; goto reg_cleanup; }

    sprintf(buf,"%s/.local/share/mime/image/",homedir);
    if(!laEnsureDir(buf)){ logPrint("Can't create dir %s\n", buf); failed=1; goto reg_cleanup; }
    strcat(buf,"ourpaint.xml");
    f=fopen(buf,"w"); if(!f){ logPrint("Can't open %s\n",buf); failed=1; goto reg_cleanup; }
    fprintf(f,"%s",OUR_MIME); fflush(f); fclose(f);

    sprintf(buf,"%s/.local/share/thumbnailers/",homedir);
    if(!laEnsureDir(buf)){ logPrint("Can't create dir %s\n", buf); failed=1; goto reg_cleanup; }
    strcat(buf,"ourpaint.thumbnailer");
    f=fopen(buf,"w"); if(!f){ logPrint("Can't open %s\n",buf); failed=1; goto reg_cleanup; }
    char* thumbstr=strSub(OUR_THUMBNAILER,"%OURPAINT_EXEC%",exepath);
    fprintf(f,"%s",thumbstr); fflush(f); fclose(f); free(thumbstr);

    sprintf(buf,"%s/.local/share/applications/",homedir);
    if(!laEnsureDir(buf)){ logPrint("Can't create dir %s\n", buf); failed=1; goto reg_cleanup; }
    strcat(buf,"ourpaint.desktop");
    f=fopen(buf,"w"); if(!f){ logPrint("Can't open %s\n",buf); failed=1; goto reg_cleanup; }
    char* deskstr=strSub(OUR_DESKTOP,"%OURPAINT_EXEC%",exepath);
    strDiscardLastSegmentSeperateBy(exepath,'/');
    char* pathstr=strSub(deskstr,"%OURPAINT_DIR%",exepath); free(deskstr);
    fprintf(f,"%s",pathstr); fflush(f); fclose(f); free(pathstr);

    system("update-mime-database ~/.local/share/mime/");
    system("xdg-mime default ourpaint.desktop image/ourpaint");

    logPrintNew("Done.\n");

reg_cleanup:
    if(failed) return 0;
    return 1;
#endif
#ifdef _WIN32
    return 0;
#endif
}
int ourinv_RegisterFileAssociations(laOperator* a, laEvent* e){
#ifdef __linux__
    if(!our_RegisterFileAssociations()){
        laEnableMessagePanel(0,0,"Error","Failed to register file associations,\n see terminal for details.",e->x,e->y,200,e);
    }else{
        laEnableMessagePanel(0,0,"Success","Successfully registered file associations.",e->x,e->y,200,e);
    }
    Our->FileRegistered=our_FileAssociationsRegistered(); laNotifyUsers("our.preferences.file_registered");
#endif
#ifdef _WIN32
    laEnableMessagePanel(0, 0, "Error", "Feature not supported yet.", e->x, e->y, 200, e);
#endif
    return LA_FINISHED;
}

int ourProcessInitArgs(int argc, char* argv[]){
    if(argc == 4 && strstr(argv[1],"--t")==argv[1]){
        FILE* fp=fopen(argv[2],"rb"); if(!fp){ printf("Can't open file %s\n",argv[2]); return -1; }
        void* data=0; size_t size=0;
        if(laExtractQuickRaw(fp,"our.thumbnail",&data,&size)){
            FILE* thumb = fopen(argv[3],"wb");
            if(thumb){
                fwrite(data, size, 1, thumb);
                fclose(thumb);
            }
            free(data);
        }else{
            printf("File doesn't have a thumbnail.\n",argv[2]);
        }
        fclose(fp);
        return -1;
    }
    return 0;
}

void ourPreFrame(){
    if(MAIN.GraphNeedsRebuild){ ourRebuildBrushEval(); }
}
void ourPushEverything(){
    laRecordDifferences(0,"our.canvas.layers");laRecordDifferences(0,"our.canvas.current_layer");
    laFreeOlderDifferences(0);
    for(OurLayer* ol=Our->Layers.pFirst;ol;ol=ol->Item.pNext){ our_LayerRefreshLocal(ol); }
}
void ourPreSave(){
    Our->SaveFailed=0;
}
void ourPostSave(){
    if(Our->SaveFailed){
        laMarkMemChanged(Our->CanvasSaverDummyList.pFirst);
        laEvent e={0}; e.type=LA_MOUSEMOVE;
        our_ShowAllocationError(&e);
    }
    Our->SaveFailed=0;
}
void ourCleanUp(){
    while(Our->Layers.pFirst){ our_RemoveLayer(Our->Layers.pFirst,1); }
    while(Our->Brushes.pFirst){ our_RemoveBrush(Our->Brushes.pFirst); }
    free(Our->icc_Clay);free(Our->icc_sRGB);free(Our->icc_LinearClay);free(Our->icc_LinearsRGB);
    free(Our->icc_LinearD65P3);free(Our->icc_D65P3);
    tnsDeleteTexture(Our->SmudgeTexture);
    glDeleteShader(Our->CanvasShader); glDeleteProgram(Our->CanvasProgram);
    glDeleteShader(Our->CompositionShader); glDeleteProgram(Our->CompositionProgram);
    arrFree(&Our->Dabs,&Our->MaxDab);
}

void ourRegisterEverything(){
    laPropContainer* pc; laKeyMapper* km; laProp* p; laSubProp* sp; laOperatorType* at;

    laCreateOperatorType("OUR_show_splash","Show Splash","Show splash screen",0,0,0,ourinv_ShowSplash,0,0,0);
    laCreateOperatorType("OUR_new_layer","New Layer","Create a new layer",0,0,0,ourinv_NewLayer,0,'+',0);
    laCreateOperatorType("OUR_duplicate_layer","Duplicate Layer","Duplicate a layer",0,0,0,ourinv_DuplicateLayer,0,U'⎘',0);
    laCreateOperatorType("OUR_remove_layer","Remove Layer","Remove this layer",0,0,0,ourinv_RemoveLayer,0,U'🗴',0);
    laCreateOperatorType("OUR_move_layer","Move Layer","Remove this layer",0,0,0,ourinv_MoveLayer,0,0,0);
    laCreateOperatorType("OUR_merge_layer","Merge Layer","Merge this layer with the layer below it",ourchk_MergeLayer,0,0,ourinv_MergeLayer,0,0,0);
    laCreateOperatorType("OUR_export_layer","Export Layer","Export this layer",ourchk_ExportLayer,0,0,ourinv_ExportLayer,ourmod_ExportLayer,U'🖫',0);
    at=laCreateOperatorType("OUR_import_layer","Import Layer","Import a PNG into a layer",0,0,ourexit_ImportLayer,ourinv_ImportLayer,ourmod_ImportLayer,U'🗁',0);
    at->UiDefine=ourui_ImportLayer; pc=laDefineOperatorProps(at, 1);
    laAddStringProperty(pc,"icc_name","ICC Name","The name of the icc profile comes with the image",LA_WIDGET_STRING_PLAIN,0,0,0,1,offsetof(OurPNGReadExtra,iccName),0,0,0,0,LA_READ_ONLY);
    laAddIntProperty(pc,"has_profile","Has Profile","If the importing image has a built-in icc profile",0,0,0,0,0,0,0,0,offsetof(OurPNGReadExtra,HasProfile),0,0,0,0,0,0,0,0,0,0,LA_READ_ONLY);
    laAddIntProperty(pc,"has_srgb","Has sRGB","If the importing image has a sRGB tag",0,0,0,0,0,0,0,0,offsetof(OurPNGReadExtra,HassRGB),0,0,0,0,0,0,0,0,0,0,LA_READ_ONLY);
    p=laAddEnumProperty(pc, "input_mode","Input Mode","Interpret input pixels as one of the supported formats",0,0,0,0,0,offsetof(OurPNGReadExtra,InputMode),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FLAT","Flat","Read the image as-is and don't do any color space transformations",OUR_PNG_READ_INPUT_FLAT,0);
    laAddEnumItemAs(p,"ICC","Image ICC","Use the image built-in icc profile",OUR_PNG_READ_INPUT_ICC,0);
    laAddEnumItemAs(p,"SRGB","Force sRGB","Interpret the image as sRGB regardless the image metadata",OUR_PNG_READ_INPUT_SRGB,0);
    laAddEnumItemAs(p,"LINEAR_SRGB","Force Linear sRGB","Interpret the image as Linear sRGB regardless the image metadata",OUR_PNG_READ_INPUT_LINEAR_SRGB,0);
    laAddEnumItemAs(p,"CLAY","Force Clay","Interpret the image as Clay (AdobeRGB 1998 compatible) regardless the image metadata",OUR_PNG_READ_INPUT_CLAY,0);
    laAddEnumItemAs(p,"LINEAR_CLAY","Force Linear Clay","Interpret the image as Linear Clay (AdobeRGB 1998 compatible) regardless the image metadata",OUR_PNG_READ_INPUT_LINEAR_CLAY,0);
    laAddEnumItemAs(p,"D65_P3","Force D65 P3","Interpret the image as D65 P3 regardless the image metadata",OUR_PNG_READ_INPUT_D65_P3,0);
    laAddEnumItemAs(p,"LINEAR_D65_P3","Force Linear D65 P3","Interpret the image as Linear D65 P3 regardless the image metadata",OUR_PNG_READ_INPUT_LINEAR_D65_P3,0);
    p=laAddEnumProperty(pc, "output_mode","Output Mode","Transform the input pixels to one of the supported formats",0,0,0,0,0,offsetof(OurPNGReadExtra,OutputMode),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"CANVAS","Follow Canvas","Transform the pixels into current canvas interpretation",OUR_PNG_READ_OUTPUT_CANVAS,0);
    laAddEnumItemAs(p,"LINEAR_SRGB","Linear sRGB","Write sRGB pixels values into canvas regardless of the canvas interpretation",OUR_PNG_READ_OUTPUT_LINEAR_SRGB,0);
    laAddEnumItemAs(p,"LINEAR_CLAY","Linear Clay","Write Clay (AdobeRGB 1998 compatible) pixels values into canvas regardless of the canvas interpretation",OUR_PNG_READ_OUTPUT_LINEAR_CLAY,0);
    laAddEnumItemAs(p,"LINEAR_D65_P3","Linear D65 P3","Write D65 P3 pixels values into canvas regardless of the canvas interpretation",OUR_PNG_READ_OUTPUT_LINEAR_D65_P3,0);
    laAddIntProperty(pc,"offsets","Offsets","Offsets of the imported layer (0 for default)",0,"X,Y",0,0,0,0,0,0,offsetof(OurPNGReadExtra,Offsets),0,0,2,0,0,0,0,0,0,0,0);

    laCreateOperatorType("OUR_new_brush","New Brush","Create a new brush",0,0,0,ourinv_NewBrush,0,'+',0);
    laCreateOperatorType("OUR_remove_brush","Remove Brush","Remove this brush",0,0,0,ourinv_RemoveBrush,0,U'🗴',0);
    laCreateOperatorType("OUR_duplicate_brush","Duplicate Brush","Duplicate this brush",0,0,0,ourinv_DuplicateBrush,0,U'⎘',0);
    laCreateOperatorType("OUR_move_brush","Move Brush","Remove this brush",0,0,0,ourinv_MoveBrush,0,0,0);
    laCreateOperatorType("OUR_brush_quick_switch","Brush Quick Switch","Brush quick switch",0,0,0,ourinv_BrushQuickSwitch,0,0,0);
    laCreateOperatorType("OUR_brush_resize","Brush Resize","Brush resize",0,0,0,ourinv_BrushResize,0,0,0);
    laCreateOperatorType("OUR_set_brush_number","Set Brush Number","Choose a numbered brush",0,0,0,ourinv_BrushSetNumber,0,0,0);
    laCreateOperatorType("OUR_action","Action","Doing action on a layer",0,0,0,ourinv_Action,ourmod_Action,0,LA_EXTRA_TO_PANEL);
    laCreateOperatorType("OUR_pick","Pick color","Pick color on the widget",0,0,0,ourinv_PickColor,ourmod_PickColor,0,LA_EXTRA_TO_PANEL);
    laCreateOperatorType("OUR_adjust_brush","Adjust brush","Adjust brush",0,0,0,ourinv_AdjustBrush,ourmod_AdjustBrushSize,0,LA_EXTRA_TO_PANEL);
    at=laCreateOperatorType("OUR_export_image","Export Image","Export the image",ourchk_ExportImage,0,ourexit_ExportImage,ourinv_ExportImage,ourmod_ExportImage,U'🖼',0);
    at->UiDefine=ourui_ExportImage; pc=laDefineOperatorProps(at, 1);
    p=laAddEnumProperty(pc, "bit_depth","Bit Depth","How many bits per channel should be used",0,0,0,0,0,offsetof(OurPNGWriteExtra,BitDepth),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"D8","8 Bits","Use 8 bits per channel",OUR_EXPORT_BIT_DEPTH_8,0);
    laAddEnumItemAs(p,"D16","16 Bits","Use 16 bits per channel",OUR_EXPORT_BIT_DEPTH_16,0);
    p=laAddEnumProperty(pc, "color_profile","Output Mode","Transform the input pixels to one of the supported formats",0,0,0,0,0,offsetof(OurPNGWriteExtra,ColorProfile),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FLAT","Flat","Export pixels in current canvans linear color space",OUR_EXPORT_COLOR_MODE_FLAT,0);
    laAddEnumItemAs(p,"SRGB","sRGB","Convert pixels into non-linear sRGB (Most used)",OUR_EXPORT_COLOR_MODE_SRGB,0);
    laAddEnumItemAs(p,"CLAY","Clay","Convert pixels into non-linear Clay (AdobeRGB 1998 compatible)",OUR_EXPORT_COLOR_MODE_CLAY,0);
    laAddEnumItemAs(p,"D65_P3","D65 P3","Convert pixels into non-linear D65 P3",OUR_EXPORT_COLOR_MODE_D65_P3,0);
    p=laAddEnumProperty(pc, "transparent","Transparent","Transparent background",0,0,0,0,0,offsetof(OurPNGWriteExtra,Transparent),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"OPAQUE","Opaque","Opaque background",0,0);
    laAddEnumItemAs(p,"TRANSPARENT","Transparent","TransparentBackground",1,0);
    
    laCreateOperatorType("OUR_toggle_erasing","Toggle Erasing","Toggle erasing",0,0,0,ourinv_ToggleErase,0,0,0);
    laCreateOperatorType("OUR_cycle_sketch","Cycle Sketches","Cycle sketch layer display mode",0,0,0,ourinv_CycleSketch,0,0,0);

    laCreateOperatorType("OUR_crop_to_ref","Crop To Ref","Crop to reference lines",ourchk_CropToRef,0,0,ourinv_CropToRef,0,0,0);

    laCreateOperatorType("OUR_new_pallette","New Pallette","New pallette",0,0,0,ourinv_NewPallette,0,'+',0);
    laCreateOperatorType("OUR_remove_pallette","Remove Pallette","Remove selected pallette",0,0,0,ourinv_RemovePallette,0,U'🗴',0);
    laCreateOperatorType("OUR_pallette_new_color","New Color","New color in this pallette",0,0,0,ourinv_PalletteNewColor,0,'+',0);
    laCreateOperatorType("OUR_pallette_remove_color","Remove Color","Remove this color from the pallette",0,0,0,ourinv_PalletteRemoveColor,0,U'🗴',0);

    laCreateOperatorType("OUR_clear_empty_tiles","Clear Empty Tiles","Clear empty tiles in this image",0,0,0,ourinv_ClearEmptyTiles,0,U'🧹',0);

    laCreateOperatorType("OUR_register_file_associations","Register File Associations","Register file associations to current user",0,0,0,ourinv_RegisterFileAssociations,0,0,0);

    laRegisterUiTemplate("panel_canvas", "Canvas", ourui_CanvasPanel, 0, 0,"Our Paint", GL_RGBA16F,25,25);
    laRegisterUiTemplate("panel_thumbnail", "Thumbnail", ourui_ThumbnailPanel, 0, 0, 0, GL_RGBA16F,25,25);
    laRegisterUiTemplate("panel_layers", "Layers", ourui_LayersPanel, 0, 0,0, 0,10,15);
    laRegisterUiTemplate("panel_tools", "Tools", ourui_ToolsPanel, 0, 0,0, 0,10,20);
    laRegisterUiTemplate("panel_brushes", "Brushes", ourui_BrushesPanel, 0, 0,0, 0,10,15);
    laRegisterUiTemplate("panel_color", "Color", ourui_ColorPanel, 0, 0,0, GL_RGBA16F,0,0);
    laRegisterUiTemplate("panel_pallettes", "Pallettes", ourui_PallettesPanel, 0, 0,0, GL_RGBA16F,0,0);
    laRegisterUiTemplate("panel_brush_nodes", "Brush Nodes", ourui_BrushPage, 0, 0,0, 0,25,30);
    laRegisterUiTemplate("panel_notes", "Notes", ourui_NotesPanel, 0, 0,0, 0,15,15);
    
    pc=laDefineRoot();
    laAddSubGroup(pc,"our","Our","OurPaint main","our_paint",0,0,0,-1,ourget_our,0,0,0,0,0,0,LA_UDF_SINGLE);

    pc=laAddPropertyContainer("our_paint","Our Paint","OurPaint main",0,0,sizeof(OurPaint),0,0,1);
    laAddRawProperty(pc,"thumbnail","Thumbnail","Thumbnail of this file",0,0,ourgetraw_FileThumbnail,oursetraw_FileThumbnail,LA_READ_ONLY);
    laAddSubGroup(pc,"canvas_notify","Canvas Notify","Property used to notify canvas redraw","our_canvas",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL|LA_UDF_IGNORE);
    laAddSubGroup(pc,"canvas","Canvas","OurPaint canvas","our_canvas",0,0,0,0,0,0,0,0,ourgetstate_Canvas,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"tools","Tools","OurPaint tools","our_tools",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"preferences","Preferences","OurPaint preferences","our_preferences",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddFloatProperty(pc,"current_color","Current Color","Current color used to paint",0,"R,G,B",0,1,0,0.05,0.8,0,offsetof(OurPaint,CurrentColor),0,0,3,0,0,0,0,0,0,0,LA_PROP_IS_LINEAR_SRGB);
    laAddFloatProperty(pc,"color_boost","Boost","Color boost to over 1.0",0,0,"x",5,0,0.05,0.5,0,0,ourget_ColorBoost,ourset_ColorBoost,0,0,0,0,0,0,0,0,LA_PROP_IS_LINEAR_SRGB);
    p=laAddEnumProperty(pc,"tool","Tool","Tool to use on the canvas",0,0,0,0,0,offsetof(OurPaint,Tool),0,ourset_Tool,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"PAINT","Paint","Paint stuff on the canvas",OUR_TOOL_PAINT,U'🖌');
    laAddEnumItemAs(p,"CROP","Cropping","Crop the focused region",OUR_TOOL_CROP,U'🖼');
    laAddEnumItemAs(p,"MOVE","Moving","Moving the layer",OUR_TOOL_MOVE,U'🤚');
    p=laAddEnumProperty(pc,"lock_background","Lock background","Lock background color to prevent accidental changes",0,0,0,0,0,offsetof(OurPaint,LockBackground),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","Unlocked","You can change background color",0,0);
    laAddEnumItemAs(p,"LOCK","Locked","Background color is locked to prevent accidental changes",1,U'🔏');
    p=laAddEnumProperty(pc,"erasing","Erasing","Is in erasing mode",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,Erasing),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","Draw","Is drawing mode",0,0);
    laAddEnumItemAs(p,"TRUE","Erase","Is erasing mode",1,0);
    p=laAddEnumProperty(pc,"brush_mix","Brush Mix","Brush mixing method",0,0,0,0,0,offsetof(OurPaint,BrushMix),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NORMAL","Normal","Brush operates normally",0,U'🖌');
    laAddEnumItemAs(p,"LOCK_ALPHA","Alpha","Locks alpha channel",1,U'⮻');
    laAddEnumItemAs(p,"TINT","Tint","Locks alpha channel and the brightness of the color",2,U'🌈');
    laAddEnumItemAs(p,"ADD","Accumulate","Accumulate values",3,U'🔦');
    p=laAddEnumProperty(pc, "brush_page","Brush Page","Show brushes in pages",0,0,0,0,0,offsetof(OurPaint,BrushPage),0,ourset_BrushPage,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"ALL","~","Show all brushes",0,'~');
    laAddEnumItemAs(p,"P1","A","Show brush page A",1,'A');
    laAddEnumItemAs(p,"P2","B","Show brush page B",2,'B');
    laAddEnumItemAs(p,"P3","C","Show brush page C",3,'C');
    laAddEnumItemAs(p,"LIST","=","Show brushes as a list",OUR_BRUSH_PAGE_LIST,L'☰');

    pc=laAddPropertyContainer("our_preferences","Our Preferences","OurPaint preferences",0,0,sizeof(OurPaint),0,0,1);
    laPropContainerExtraFunctions(pc,0,ourreset_Preferences,0,0,0);
    laAddFloatProperty(pc,"brush_size","Brush Size","Brush size for drawing",0,0,0,10,0,0.05,2,0,offsetof(OurPaint,BrushSize),0,ourset_BrushSize,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"enable_brush_circle","Brush Circle","Enable brush circle when hovering",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,EnableBrushCircle),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Don't show brush circle",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Show brush circle on hover",1,0);
    p=laAddEnumProperty(pc,"allow_none_pressure","Allow Non-pressure","Allow non-pressure events, this enables mouse painting.",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,AllowNonPressure),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Don't allow non-pressure device inputs",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Allow non-pressure device inputs such as a mouse",1,0);
    laAddIntProperty(pc,"bad_event_tolerance","Bad Event Tolerance","Try to recieve more events before painting starts to get around some stylus hardware issue",0,0,0,16,0,1,0,0,offsetof(OurPaint,BadEventsLimit),0,0,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"show_debug_tiles","Show debug tiles","Whether to show debug tiles",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,ShowTiles),0,ourset_ShowTiles,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Don't show debug tiles on the canvas",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Show debug tiles on the canvas",1,0);
    p=laAddEnumProperty(pc,"export_default_bit_depth","Export Default Bit Depth","Default bit depth when exporting images",0,0,0,0,0,offsetof(OurPaint,DefaultBitDepth),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"D8","8 Bits","Use 8 bits per channel",OUR_EXPORT_BIT_DEPTH_8,0);
    laAddEnumItemAs(p,"D16","16 Bits","Use 16 bits per channel",OUR_EXPORT_BIT_DEPTH_16,0);
    p=laAddEnumProperty(pc, "export_default_color_profile","Export Default Color Profile","Default color profile to use when exporting images",0,0,0,0,0,offsetof(OurPaint,DefaultColorProfile),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FLAT","Flat","Export pixels in current canvans linear color space",OUR_EXPORT_COLOR_MODE_FLAT,0);
    laAddEnumItemAs(p,"SRGB","sRGB","Convert pixels into non-linear sRGB (Most used)",OUR_EXPORT_COLOR_MODE_SRGB,0);
    laAddEnumItemAs(p,"CLAY","Clay","Convert pixels into non-linear Clay (AdobeRGB 1998 compatible)",OUR_EXPORT_COLOR_MODE_CLAY,0);
    laAddEnumItemAs(p,"D65_P3","D65 P3","Convert pixels into non-linear D65 P3",OUR_EXPORT_COLOR_MODE_D65_P3,0);
    laAddIntProperty(pc,"paint_undo_limit","Paint Undo Limit","Undo step limit for painting actions.",0,0," Steps",256,5,1,100,0,offsetof(OurPaint,PaintUndoLimit),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"canvas_default_scale","Canvas Default Scale","Default scale of the canvas",0,0,0,4,0.25,0.1,0.5,0,offsetof(OurPaint,DefaultScale),0,0,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"spectral_mode","Spectral Brush","Use spectral mixing in brush strokes",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,SpectralMode),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Use regular RGB mixing for brushes",0,0);
    laAddEnumItemAs(p,"SPECTRAL","Spectral","Use spectral mixing for brushes",1,0);
    p=laAddEnumProperty(pc,"brush_numbers_on_header","Brush Numbers","Show brush numbers on header",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,BrushNumbersOnHeader),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Hide brush numbers on header",0,0);
    laAddEnumItemAs(p,"SHOWN","Shown","Show brush numbers on header",1,0);
    p=laAddEnumProperty(pc,"mix_mode_on_header","Mix Modes","Show mix modes on header",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,MixModeOnHeader),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Hide mix modes on header",0,0);
    laAddEnumItemAs(p,"SHOWN","Shown","Show mix modes on header",1,0);
    p=laAddEnumProperty(pc,"tools_on_header","Tools","Show tool selector on header",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,ToolsOnHeader),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Hide tool selector on header",0,0);
    laAddEnumItemAs(p,"SHOWN","Shown","Show tool selector on header",1,0);
    p=laAddEnumProperty(pc,"undo_on_header","Undo","Show undo buttons on header",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,UndoOnHeader),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Hide undo buttons on header",0,0);
    laAddEnumItemAs(p,"SHOWN","Shown","Show undo buttons on header",1,0);
    laAddFloatProperty(pc,"smoothness","Smoothness","Smoothness of global brush input",0,0, 0,1,0,0.05,0,0,offsetof(OurPaint,Smoothness),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"hardness","Strength","Pressure strength of global brush input",0,0, 0,1,-1,0.05,0,0,offsetof(OurPaint,Hardness),0,0,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"show_stripes","Ref Stripes","Whether to show visual reference stripes",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,ShowStripes),0,ourset_ShowStripes,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Don't show visual reference stripes",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Show visual reference stripes at the top and bottom of the canvas",1,0);
    p=laAddEnumProperty(pc,"show_grid","Ref Grids","Whether to show visual reference grids",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,ShowGrid),0,ourset_ShowGrid,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Don't show visual reference grids",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Show visual reference grid on top of the canvas",1,0);
    p=laAddEnumProperty(pc, "brush_number","Brush Number","Select brush radius by number",0,0,0,0,0,offsetof(OurPaint,BrushNumber),0,ourset_BrushNumber,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FREE","#","Brush size is freely adjustable",0,0);
    laAddEnumItemAs(p,"NUMBER0","0","Use brush number 0",1, 0);
    laAddEnumItemAs(p,"NUMBER1","1","Use brush number 1",2, 0);
    laAddEnumItemAs(p,"NUMBER2","2","Use brush number 2",3, 0);
    laAddEnumItemAs(p,"NUMBER3","3","Use brush number 3",4, 0);
    laAddEnumItemAs(p,"NUMBER4","4","Use brush number 4",5, 0);
    laAddEnumItemAs(p,"NUMBER5","5","Use brush number 5",6, 0);
    laAddEnumItemAs(p,"NUMBER6","6","Use brush number 6",7, 0);
    laAddEnumItemAs(p,"NUMBER7","7","Use brush number 7",8, 0);
    laAddEnumItemAs(p,"NUMBER8","8","Use brush number 8",9, 0);
    laAddEnumItemAs(p,"NUMBER9","9","Use brush number 9",10,0);
    p=laAddEnumProperty(pc,"multithread_write","Multi-thread Write","Whether to write layers in segments with multiple threads to increase speed",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurPaint,SegmentedWrite),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","Sequential","Write layers into a whole image",0,0);
    laAddEnumItemAs(p,"SEGMENTED","Segmented","Write layers in segmented images with multiple threads",1,0);
    p=laAddEnumProperty(pc,"file_registered","File Registered","Whether Our Paint is registered in the system",0,0,0,0,0,offsetof(OurPaint,FileRegistered),0,0,0,0,0,0,0,0,0,LA_READ_ONLY|LA_UDF_IGNORE);
    laAddEnumItemAs(p,"FALSE","Not registered","File association isn't registered",0,0);
    laAddEnumItemAs(p,"TRUE","Registered","File association is registered",1,0);
    p=laAddEnumProperty(pc,"brush_circle_tilt_mode","Brush Circle Tilt Mode","Brush circle tilt display mode",0,0,0,0,0,offsetof(OurPaint,BrushCircleTiltMode),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Only show a circle",0,0);
    laAddEnumItemAs(p,"TILT","Tilt","Brush direction line follows tilt direction",1,0);
    laAddEnumItemAs(p,"TWIST","Twist","Brush direction line follows twist direction",2,0);
    laAddEnumItemAs(p,"AUTO","Auto","Brush direction line determines automatically whether to show tilt or twist",3,0);

    pc=laAddPropertyContainer("our_tools","Our Tools","OurPaint tools",0,0,sizeof(OurPaint),0,0,1);
    laPropContainerExtraFunctions(pc,0,0,0,ourpropagate_Tools,0);
    sp=laAddSubGroup(pc,"brushes","Brushes","Brushes","our_brush",0,0,ourui_Brush,offsetof(OurPaint,CurrentBrush),0,0,0,ourset_CurrentBrush,ourgetstate_Brush,0,offsetof(OurPaint,Brushes),0);
    sp->UiFilter=ourfilter_BrushInPage;
    laAddSubGroup(pc,"current_brush","Current Brush","Current brush","our_brush",0,0,0,offsetof(OurPaint,CurrentBrush),ourget_FirstBrush,0,laget_ListNext,ourset_CurrentBrush,0,0,0,LA_UDF_REFER);
    sp=laAddSubGroup(pc,"pallettes","Pallettes","Pallettes","our_pallette",0,0,ourui_Pallette,offsetof(OurPaint,CurrentPallette),0,0,0,ourset_CurrentPallette,ourgetstate_Pallette,0,offsetof(OurPaint,Pallettes),0);
    //sp->UiFilter=ourfilter_BrushInPage;
    laAddSubGroup(pc,"current_pallette","Current Pallette","Current pallette","our_pallette",0,0,0,offsetof(OurPaint,CurrentPallette),ourget_FirstPallette,0,laget_ListNext,ourset_CurrentPallette,0,0,0,LA_UDF_REFER);
    
    pc=laAddPropertyContainer("our_brush","Our Brush","OurPaint brush",0,0,sizeof(OurBrush),0,0,2);
    laAddStringProperty(pc,"name","Name","Name of the brush",0,0,0,0,1,offsetof(OurBrush,Name),0,0,0,0,LA_AS_IDENTIFIER);
    laAddIntProperty(pc,"__move","Move Slider","Move Slider",LA_WIDGET_HEIGHT_ADJUSTER,0,0,0,0,0,0,0,0,0,ourset_BrushMove,0,0,0,0,0,0,0,0,LA_UDF_IGNORE);
    laAddIntProperty(pc,"binding","Binding","Keyboard binding for shortcut access of the brush",0,0,0,9,-1,1,0,0,offsetof(OurBrush,Binding),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"size_offset","Size Offset","Base size(radius) offset of the brush, in 2^n px",0,0,0,5,-5,0.05,0,0,offsetof(OurBrush,SizeOffset),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"transparency","Transparency","Transparency of a dab",0,0,0,1,0,0.05,0.5,0,offsetof(OurBrush,Transparency),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"hardness","Hardness","Hardness of the brush",0,0,0,1,0,0.05,0.95,0,offsetof(OurBrush,Hardness),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"smudge","Smudge","Smudge of the brush",0,0,0,1,0,0.05,0.95,0,offsetof(OurBrush,Smudge),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"dabs_per_size","Dabs Per Size","How many dabs per size of the brush",0,0,0,0,0,0,0,0,offsetof(OurBrush,DabsPerSize),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"smudge_resample_length","Smudge Resample Length","How long of a distance (based on size) should elapse before resampling smudge",0,0,0,0,0,0,0,0,offsetof(OurBrush,SmudgeResampleLength),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"slender","Slender","Slenderness of the brush",0,0, 0,10,0,0.1,0,0,offsetof(OurBrush,Slender),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"angle","Angle","Angle of the brush",0,0, 0,TNS_PI,-TNS_PI,0.1,0,0,offsetof(OurBrush,Angle),0,0,0,0,0,0,0,0,0,0,LA_RAD_ANGLE);
    laAddFloatProperty(pc,"smoothness","Smoothness","Smoothness of the brush",0,0, 0,1,0,0.05,0,0,offsetof(OurBrush,Smoothness),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"force","Force","How hard the brush is pushed against canvas texture",0,0,0,1,0,0.05,0,0,offsetof(OurBrush,Force),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"gunkyness","Gunkyness","How will the brush stick to the canvas texture",0,0, 0,1,-1,0.05,0,0,offsetof(OurBrush,Gunkyness),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"c1","C1","Custom brush input 1",0,0, 0,0,0,0.05,0,0,offsetof(OurBrush,Custom1),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"c2","C2","Custom brush input 2",0,0, 0,0,0,0.05,0,0,offsetof(OurBrush,Custom2),0,0,0,0,0,0,0,0,0,0,0);
    laAddStringProperty(pc,"c1_name","C1 Name","Custom input 1 name",0,0,0,0,1,offsetof(OurBrush,Custom1Name),0,0,0,0,0);
    laAddStringProperty(pc,"c2_name","C2 Name","Custom input 2 name",0,0,0,0,1,offsetof(OurBrush,Custom2Name),0,0,0,0,0);
    p=laAddEnumProperty(pc,"pressure_size","Pressure Size","Use pen pressure to control size",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureSize),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_transparency","Pressure Transparency","Use pen pressure to control transparency",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureTransparency),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_hardness","Pressure Hardness","Use pen pressure to control hardness",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureHardness),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_smudge","Pressure Smudge","Use pen pressure to control smudging",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureSmudge),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"pressure_force","Pressure Force","Use pen pressure to control dab force",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,PressureForce),0,0,0,0,0,0,0,0,0,0);
    OUR_ADD_PRESSURE_SWITCH(p);
    p=laAddEnumProperty(pc,"twist_angle","Twist Angle","Use pen twist to control dab angle",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,TwistAngle),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Not using twist",0,0);
    laAddEnumItemAs(p,"ENABLED","Enabled","Using twist",1,0);
    p=laAddEnumProperty(pc,"use_nodes","Use Nodes","Use nodes to control brush dynamics",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,UseNodes),0,0,0,0,0,0,0,0,0,0);
    p->ElementBytes=2;
    laAddEnumItemAs(p,"NONE","None","Not using nodes",0,0);
    laAddEnumItemAs(p,"ENABLED","Enabled","Using nodes",1,0);
    laAddSubGroup(pc,"rack_page","Rack Page","Nodes rack page of this brush","la_rack_page",0,0,laui_RackPage,offsetof(OurBrush,Rack),0,0,0,0,0,0,0,LA_UDF_SINGLE|LA_HIDE_IN_SAVE);
    p=laAddEnumProperty(pc,"default_as_eraser","Default as eraser","Use this brush as a eraser by default",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,DefaultAsEraser),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Default as brush",0,0);
    laAddEnumItemAs(p,"ENABLED","Enabled","Default as eraser",1,0);
    p=laAddEnumProperty(pc, "show_in_pages","Pages","Show in pages",0,0,0,0,0,0,0,0,3,0,ourset_BrushShowInPages,ourget_BrushShowInPages,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Don't show brush in this page",0,' ');
    laAddEnumItemAs(p,"SHOWN","Shown","Show brush in this page",1,'*');
    p=laAddEnumProperty(pc,"offset_follow_pen_tilt","Follow Tilt","Brush center visual offset direction follows pen tilt",LA_WIDGET_ENUM_HIGHLIGHT,0,0,0,0,offsetof(OurBrush,OffsetFollowPenTilt),0,0,0,0,0,0,0,0,0,0);
    p->ElementBytes=2;
    laAddEnumItemAs(p,"NONE","None","Fixed angle",0,0);
    laAddEnumItemAs(p,"ENABLED","Enabled","Follow pen tilt",1,0);
    laAddFloatProperty(pc,"visual_offset","Offset","Visual offset of the pen dab from system cursor",0,0,0,20,0,0.1,0,0,offsetof(OurBrush,VisualOffset),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"visual_offset_angle","Angle","Visual offset angle",0,0,0,TNS_PI*2,0,0.01,TNS_PI/4,0,offsetof(OurBrush,VisualOffsetAngle),0,0,0,0,0,0,0,0,0,0,LA_RAD_ANGLE);
    laAddOperatorProperty(pc,"move","Move","Move brush","OUR_move_brush",0,0);
    laAddOperatorProperty(pc,"remove","Remove","Remove brush","OUR_remove_brush",U'🗴',0);
    laAddOperatorProperty(pc,"duplicate","Duplicate","Duplicate brush","OUR_duplicate_brush",U'⎘',0);

    pc=laAddPropertyContainer("our_pallette","Our Pallette","OurPaint pallette",0,0,sizeof(OurColorPallette),0,0,2);
    laAddStringProperty(pc,"name","Name","Name of this pallette",0,0,0,0,1,offsetof(OurColorPallette,Name),0,0,0,0,LA_AS_IDENTIFIER);
    laAddSubGroup(pc,"colors","Colors","Colors in this pallette","our_color_item",0,0,0,-1,0,0,0,ourset_PalletteColor,0,0,offsetof(OurColorPallette,Colors),0);

    pc=laAddPropertyContainer("our_color_item","Our Color Item","OurPaint pallette color item",0,0,sizeof(OurColorItem),0,0,1);
    laAddFloatProperty(pc,"color","Color","Color",LA_WIDGET_FLOAT_COLOR,0,0,0,0,0,0,0,offsetof(OurColorItem,Color),0,0,3,0,0,0,0,0,0,0,LA_PROP_IS_LINEAR_SRGB);
    laAddSubGroup(pc,"parent","Parent","Parent pallette","our_pallette",0,0,0,offsetof(OurColorItem,Parent),0,0,0,0,0,0,0,LA_UDF_REFER|LA_READ_ONLY);
    laAddOperatorProperty(pc,"remove","Remove","Remove this color item","OUR_pallette_remove_color",U'🗴',0);

    pc=laAddPropertyContainer("our_canvas","Our Canvas","OurPaint canvas",0,0,sizeof(OurPaint),ourpost_Canvas,0,1);
    laPropContainerExtraFunctions(pc,0,ourreset_Canvas,0,0,0);
    laAddFloatProperty(pc,"brush_base_size","Brush Base Size","Brush base size for using numbered sizes",0,0,0,5,0,0.05,2,0,offsetof(OurPaint,BrushBaseSize),0,ourset_BrushBaseSize,0,0,0,0,0,0,0,0,0);
    Our->CanvasSaverDummyProp=laPropContainerManageable(pc, offsetof(OurPaint,CanvasSaverDummyList));
    laAddStringProperty(pc,"identifier","Identifier","Canvas identifier placeholder",0,0,0,0,0,0,0,ourget_CanvasIdentifier,0,0,0);
    laAddStringProperty(pc,"notes","Notes","Notes of this painting",LA_WIDGET_STRING_MULTI,0,0,0,1,offsetof(OurPaint,Notes),0,0,0,0,0);
    laAddIntProperty(pc,"canvas_version","Canvas Version",0,0,0,0,0,0,0,0,0,offsetof(OurPaint,CanvasVersion),ourget_CanvasVersion,0,0,0,0,0,0,0,0,0,LA_READ_ONLY);
    laAddSubGroup(pc,"layers","Layers","Layers","our_layer",0,0,ourui_Layer,offsetof(OurPaint,CurrentLayer),0,0,0,0,0,0,offsetof(OurPaint,Layers),LA_PROP_READ_PROGRESS);
    laAddSubGroup(pc,"current_layer","Current Layer","Current layer","our_layer",0,0,0,offsetof(OurPaint,CurrentLayer),ourget_FirstLayer,0,laget_ListNext,0,0,0,0,LA_UDF_REFER);
    laAddIntProperty(pc,"size","Size","Size of the cropping area",0,"W,H","px",0,0,0,2400,0,offsetof(OurPaint,W),0,0,2,0,0,0,0,ourset_CanvasSize,0,0,0);
    laAddIntProperty(pc,"position","Position","Position of the cropping area",0,"X,Y","px",0,0,0,2400,0,offsetof(OurPaint,X),0,0,2,0,0,0,0,ourset_CanvasPosition,0,0,0);
    laAddFloatProperty(pc,"background_color","Background Color","Background color of the canvas",0,"R,G,B",0,1,0,0.05,0.8,0,offsetof(OurPaint,BackgroundColor),0,0,3,0,0,0,0,ourset_BackgroundColor,0,0,LA_PROP_IS_LINEAR_SRGB);
    p=laAddEnumProperty(pc,"background_type","Background Type","Background texture type",0,0,0,0,0,offsetof(OurPaint,BackgroundType),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","No textured background",0,0);
    laAddEnumItemAs(p,"CANVAS","Canvas","Background mimics canvas texture",1,0);
    laAddEnumItemAs(p,"PAPER","Paper","Background mimics paper texture",2,0);
    laAddIntProperty(pc,"background_random","Random","Background random pattern value",0,0,0,0,0,0,0,0,offsetof(OurPaint,BackgroundRandom),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"background_factor","Factor","Background effect factor",0,0,0,1,0,0,0,0,offsetof(OurPaint,BackgroundFactor),0,0,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"border_alpha","Border Alpha","Alpha of the border region around the canvas",0,0,0,1,0,0.05,0.5,0,offsetof(OurPaint,BorderAlpha),0,ourset_BorderAlpha,0,0,0,0,0,0,0,0,0);
    laAddFloatProperty(pc,"border_fade_width","Fade Width","Fading of the border",0,0,0,1,0,0.05,0,0,offsetof(OurPaint,BorderFadeWidth),0,ourset_BorderFadeWidth,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"show_border","Show Border","Whether to show border on the canvas",0,0,0,0,0,offsetof(OurPaint,ShowBorder),0,ourset_ShowBorder,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FALSE","No","Dont' show border on the canvas",0,0);
    laAddEnumItemAs(p,"TRUE","Yes","Show border on the canvas",1,0);
    p=laAddEnumProperty(pc,"color_interpretation","Color Interpretation","Interpret the color values on this canvas as in which color space",0,0,0,0,0,offsetof(OurPaint,ColorInterpretation),0,ourset_ColorInterpretation,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"LINEAR_SRGB","Linear sRGB","Interpret the color values as if they are in Linear sRGB color space",OUR_CANVAS_INTERPRETATION_SRGB,0);
    laAddEnumItemAs(p,"LINEAR_CLAY","Linear Clay","Interpret the color values as if they are in Linear Clay color space (AdobeRGB 1998 compatible)",OUR_CANVAS_INTERPRETATION_CLAY,0);
    laAddEnumItemAs(p,"LINEAR_D65_P3","Linear D65 P3","Interpret the color values as if they are in Linear D65 P3 color space",OUR_CANVAS_INTERPRETATION_D65_P3,0);
    laAddFloatProperty(pc,"ref_alpha","Ref Alpha","Alpha of the reference lines",0,0,0,1,0,0.05,0.75,0,offsetof(OurPaint,RefAlpha),0,ourset_RefAlpha,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"ref_mode","Show Reference Lines","Whether to show reference lines",0,0,0,0,0,offsetof(OurPaint,ShowRef),0,ourset_ShowRef,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","None","Don't show reference lines",0,0);
    laAddEnumItemAs(p,"BORDER","Border","Show reference lines like paper boundaries",1,0);
    laAddEnumItemAs(p,"SPREAD","Spread","Show double page spread",2,0);
    p=laAddEnumProperty(pc,"ref_category","Category","Dimension category of the reference block",0,0,0,0,0,offsetof(OurPaint,RefCategory),0,ourset_RefCategory,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"A","A","A series ISO 216 / DIN 476",0,0);
    laAddEnumItemAs(p,"B","B","B series ISO 216 / DIN 476",1,0);
    laAddEnumItemAs(p,"K","2nK","East-Asian 2nK paper sizes",2,0);
    p=laAddEnumProperty(pc,"ref_size","Size","Reference block size",0,0,0,0,0,offsetof(OurPaint,RefSize),0,ourset_RefSize,0,0,0,0,0,0,0,0);
#define _STR(a) #a
#define ADD_SIZE(a) laAddEnumItemAs(p,"SIZE"_STR(a),_STR(a),"SIZE"_STR(a),a,0);
    ADD_SIZE(0);ADD_SIZE(1);ADD_SIZE(2);ADD_SIZE(3);ADD_SIZE(4);ADD_SIZE(5);ADD_SIZE(6);ADD_SIZE(7);
#undef ADD_SIZE
#undef _STR
    p=laAddEnumProperty(pc,"ref_orientation","Orientation","Orientation of the reference block",0,0,0,0,0,offsetof(OurPaint,RefOrientation),0,ourset_RefOrientation,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"H","Horizontal","Horizontal",0,L'▭');
    laAddEnumItemAs(p,"V","Vertical","Vertical",1,L'▯');
    laAddFloatProperty(pc,"ref_margins","Margins","Margins of the reference block",0,"L/R,T/B","cm",0,0,0,0,0,offsetof(OurPaint,RefMargins),0,0,2,0,0,0,0,ourset_RefMargins,0,0,0);
    laAddFloatProperty(pc,"ref_paddings","Paddings","Paddings of the reference block",0,"L/R,T/B","cm",0,0,0,0,0,offsetof(OurPaint,RefPaddings),0,0,2,0,0,0,0,ourset_RefPaddings,0,0,0);
    laAddFloatProperty(pc,"ref_middle_margin","Middle Margin","Margin in the middle of the spread",0,0,"cm",0,0,0,0,0,offsetof(OurPaint,RefMargins[2]),0,ourset_RefMiddleMargin,0,0,0,0,0,0,0,0,0);
    laAddIntProperty(pc,"ref_biases","Reference Biases","Position biases when reading reference block",0,0,0,0,0,0,0,0,offsetof(OurPaint,RefBiases),0,0,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"ref_cut_half","Cut Half","Cut to half of the image",0,0,0,0,0,offsetof(OurPaint,RefCutHalf),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"FULL","Full","Use full image",0,0);
    laAddEnumItemAs(p,"LEFT","Left","Cut to left portion",1,0);
    laAddEnumItemAs(p,"RIGHT","Right","Cut to right portion",2,0);
    p=laAddEnumProperty(pc,"sketch_mode","Sketch Mode","Show sketch layers differently",0,0,0,0,0,offsetof(OurPaint,SketchMode),0,ourset_ShowSketch,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NORMAL","Normal","Show sketch layers as normal layers",0,0);
    laAddEnumItemAs(p,"FULL","Full","Show sketch layers in full opacity",1,0);
    laAddEnumItemAs(p,"NONE","None","Show double page spread",2,0);

    pc=laAddPropertyContainer("our_layer","Our Layer","OurPaint layer",0,0,sizeof(OurLayer),0,0,1);
    laPropContainerExtraFunctions(pc,ourbeforefree_Layer,ourbeforefree_Layer,0,0,0);
    laAddStringProperty(pc,"name","Name","Name of the layer",0,0,0,0,1,offsetof(OurLayer,Name),0,0,0,0,LA_AS_IDENTIFIER);
    laAddIntProperty(pc,"__move","Move Slider","Move Slider",LA_WIDGET_HEIGHT_ADJUSTER,0,0,0,0,0,0,0,0,0,ourset_LayerMove,0,0,0,0,0,0,0,0,LA_UDF_IGNORE);
    laAddIntProperty(pc,"offset","Offset","Offset of the layer",0,"X,Y","px",0,0,0,0,0,offsetof(OurLayer,OffsetX),0,0,2,0,0,0,0,ourset_LayerPosition,0,0,0);
    laAddIntProperty(pc,"tile_start","Tile Start","Tile starting position for loading",0,0,0,0,0,0,0,0,0,0,0,2,0,0,ourget_LayerTileStart,0,ourset_LayerTileStart,0,0,LA_UDF_ONLY);
    p=laAddEnumProperty(pc,"lock","Lock","Lock this layer",0,0,0,0,0,offsetof(OurLayer,Lock),0,0,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","Paintable","You can paint on this layer",0,U'🖌');
    laAddEnumItemAs(p,"LOCK","Locked","This layer is locked from modification",1,U'🔏');
    p=laAddEnumProperty(pc,"hide","Hide","Hide this layer",0,0,0,0,0,offsetof(OurLayer,Hide),0,ourset_LayerHide,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NONE","Visible","Layer is visible",0,U'🌑');
    laAddEnumItemAs(p,"HIDE","Hidden","Layer is hidden",1,U'🌔');
    laAddFloatProperty(pc,"transparency","Transparency","Alpha of the layer",0,0,0,1,0,0.05,1,0,offsetof(OurLayer,Transparency),0, ourset_LayerAlpha,0,0,0,0,0,0,0,0,0);
    p=laAddEnumProperty(pc,"blend_mode","Blend Mode","How this layer is blended onto the stuff below",0,0,0,0,0,offsetof(OurLayer,BlendMode),0,ourset_LayerBlendMode,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NORMAL","Normal","Normal alpha blend",OUR_BLEND_NORMAL,0);
    laAddEnumItemAs(p,"ADD","Add","Pixel values are simply added together",OUR_BLEND_ADD,0);
    laAddRawProperty(pc,"segmented_info","Segmented Info","Image segmented info",0,0,ourget_LayerImageSegmentedInfo,ourset_LayerImageSegmentedInfo,LA_UDF_ONLY);
    p=laAddRawProperty(pc,"image","Image","The image data of this tile",0,0,ourget_LayerImage,ourset_LayerImage,LA_UDF_ONLY);
    laRawPropertyExtraFunctions(p,ourget_LayerImageSegmented,ourget_LayerImageShouldSegment);
    laAddOperatorProperty(pc,"move","Move","Move Layer","OUR_move_layer",0,0);
    laAddOperatorProperty(pc,"remove","Remove","Remove layer","OUR_remove_layer",U'🗴',0);
    laAddOperatorProperty(pc,"merge","Merge","Merge layer","OUR_merge_layer",U'🠳',0);
    laAddOperatorProperty(pc,"duplicate","Duplicate","Duplicate layer","OUR_duplicate_layer",U'⎘',0);
    p=laAddEnumProperty(pc,"as_sketch","As Sketch","As sketch layer (for quick toggle)",0,0,0,0,0,offsetof(OurLayer,AsSketch),0,ourset_LayerAsSketch,0,0,0,0,0,0,0,0);
    laAddEnumItemAs(p,"NORMAL","Normal","Layer is normal",0,U'🖌');
    laAddEnumItemAs(p,"SKETCH","Sketch","Layer is a sketch layer",1,U'🖉');
    
    laCanvasTemplate* ct=laRegisterCanvasTemplate("our_CanvasDraw", "our_canvas", ourextramod_Canvas, our_CanvasDrawCanvas, our_CanvasDrawOverlay, our_CanvasDrawInit, la_CanvasDestroy);
    pc = laCanvasHasExtraProps(ct,sizeof(OurCanvasDraw),2);
    km = &ct->KeyMapper;
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_DOWN, 0, "direction=out");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_UP, 0, "direction=in");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, LA_KEY_CTRL, LA_M_MOUSE_DOWN, 0, "mode=mouse;lock=true;");
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, LA_KEY_ALT, LA_L_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_M_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_KEY_DOWN, LA_PANNING_LEFT, "pan=left");
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_KEY_DOWN, LA_PANNING_RIGHT, "pan=right");
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_KEY_DOWN, LA_PANNING_UP, "pan=up");
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_KEY_DOWN, LA_PANNING_DOWN, "pan=down");
    laAssignNewKey(km, 0, "OUR_action", LA_KM_SEL_UI_EXTRA, 0, LA_L_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "OUR_pick", LA_KM_SEL_UI_EXTRA, 0, LA_R_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "OUR_pick", LA_KM_SEL_UI_EXTRA, LA_KEY_CTRL, LA_L_MOUSE_DOWN, 0, 0);

    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_ZOOM_OUT, "direction=out");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_ZOOM_IN, "direction=in");
    laAssignNewKey(km, 0, "OUR_pick", LA_KM_SEL_UI_EXTRA, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_PICK, 0);
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_MOVE, 0);
    laAssignNewKey(km, 0, "OUR_adjust_brush", LA_KM_SEL_UI_EXTRA, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_ADJUST, 0);

    km=&MAIN.KeyMap; char buf[128];
    for(int i=0;i<=9;i++){
        sprintf(buf,"binding=%d",i); laAssignNewKey(km, 0, "OUR_brush_quick_switch", 0, 0, LA_KEY_DOWN, '0'+i, buf);
    }
    laAssignNewKey(km, 0, "OUR_brush_resize", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_BRUSH_SMALLER, "direction=smaller");
    laAssignNewKey(km, 0, "OUR_brush_resize", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_BRUSH_BIGGER, "direction=bigger");
    laAssignNewKey(km, 0, "OUR_toggle_erasing", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_TOGGLE_ERASING, 0);
    laAssignNewKey(km, 0, "OUR_cycle_sketch", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_TOGGLE_SKETCH, 0);
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_0, "number=0");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_1, "number=1");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_2, "number=2");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_3, "number=3");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_4, "number=4");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_5, "number=5");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_6, "number=6");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_7, "number=7");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_8, "number=8");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_NUMBER_9, "number=9");
    laAssignNewKey(km, 0, "OUR_set_brush_number", 0, 0, LA_SIGNAL_EVENT, OUR_SIGNAL_SELECT_BRUSH_FREE, "number=#");

    laNewCustomSignal("our.pick",OUR_SIGNAL_PICK);
    laNewCustomSignal("our.move",OUR_SIGNAL_MOVE);
    laNewCustomSignal("our.toggle_erasing",OUR_SIGNAL_TOGGLE_ERASING);
    laNewCustomSignal("our.toggle_sketch",OUR_SIGNAL_TOGGLE_SKETCH);
    laNewCustomSignal("our.zoom_in",OUR_SIGNAL_ZOOM_IN);
    laNewCustomSignal("our.zoom_out",OUR_SIGNAL_ZOOM_OUT);
    laNewCustomSignal("our.brush_bigger",OUR_SIGNAL_BRUSH_BIGGER);
    laNewCustomSignal("our.brush_smaller",OUR_SIGNAL_BRUSH_SMALLER);
    laNewCustomSignal("our.brush_number_0",OUR_SIGNAL_SELECT_BRUSH_NUMBER_0);
    laNewCustomSignal("our.brush_number_1",OUR_SIGNAL_SELECT_BRUSH_NUMBER_1);
    laNewCustomSignal("our.brush_number_2",OUR_SIGNAL_SELECT_BRUSH_NUMBER_2);
    laNewCustomSignal("our.brush_number_3",OUR_SIGNAL_SELECT_BRUSH_NUMBER_3);
    laNewCustomSignal("our.brush_number_4",OUR_SIGNAL_SELECT_BRUSH_NUMBER_4);
    laNewCustomSignal("our.brush_number_5",OUR_SIGNAL_SELECT_BRUSH_NUMBER_5);
    laNewCustomSignal("our.brush_number_6",OUR_SIGNAL_SELECT_BRUSH_NUMBER_6);
    laNewCustomSignal("our.brush_number_7",OUR_SIGNAL_SELECT_BRUSH_NUMBER_7);
    laNewCustomSignal("our.brush_number_8",OUR_SIGNAL_SELECT_BRUSH_NUMBER_8);
    laNewCustomSignal("our.brush_number_9",OUR_SIGNAL_SELECT_BRUSH_NUMBER_9);
    laNewCustomSignal("our.brush_free",OUR_SIGNAL_SELECT_BRUSH_FREE);
    laNewCustomSignal("our.adjust",OUR_SIGNAL_ADJUST);

    laInputMapping* im=MAIN.InputMapping->CurrentInputMapping;
    if(!im) im=laNewInputMapping("Our Paint Default");
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,",",0,OUR_SIGNAL_ZOOM_OUT);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,".",0,OUR_SIGNAL_ZOOM_IN);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"[",0,OUR_SIGNAL_BRUSH_SMALLER);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"]",0,OUR_SIGNAL_BRUSH_BIGGER);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Space",0,OUR_SIGNAL_MOVE);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"s",0,OUR_SIGNAL_TOGGLE_SKETCH);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"e",0,OUR_SIGNAL_TOGGLE_ERASING);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num0",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_0);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num1",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_1);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num2",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_2);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num3",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_3);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num4",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_4);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num5",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_5);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num6",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_6);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num7",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_7);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num8",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_8);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"Num9",0,OUR_SIGNAL_SELECT_BRUSH_NUMBER_9);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"NumDot",0,OUR_SIGNAL_SELECT_BRUSH_FREE);
    laNewInputMappingEntryP(im,LA_INPUT_DEVICE_KEYBOARD,0,"f",0,OUR_SIGNAL_ADJUST);

    laAssignNewKey(km, 0, "LA_undo", 0, LA_KEY_CTRL, LA_KEY_DOWN, ']', 0);
    laAssignNewKey(km, 0, "LA_redo", 0, LA_KEY_CTRL, LA_KEY_DOWN, '[', 0);

    laSetMenuBarTemplates(ourui_MenuButtons, ourui_ToolExtras, OUR_PAINT_NAME_STRING);

    ourRegisterNodes();

    laManagedSaveProp* msp= laSaveProp("our.canvas");
    laSaveAlongside(msp,"our.thumbnail");
    laSetThumbnailProp("our.thumbnail");
    laSaveProp("our.tools");

    laGetSaverDummy(Our,Our->CanvasSaverDummyProp);

    laAddExtraExtension(LA_FILETYPE_UDF,"ourpaint","ourbrush",0ll);
    laAddExtraPreferencePath("our.preferences");
    laAddExtraPreferencePage("Our Paint",ourui_OurPreference);

    laSetAboutTemplates(ourui_AboutContent,ourui_AboutVersion,ourui_AboutAuthor);

    laSetFrameCallbacks(ourPreFrame,0,0);
    laSetDiffCallback(ourPushEverything);
    laSetSaveCallback(ourPreSave, ourPostSave);
    laSetCleanupCallback(ourCleanUp);

    ourMakeTranslations_es_ES();
    ourMakeTranslations_zh_hans();
}

#ifdef LAGUI_ANDROID
static void android_ensure_asset_to_public_dir(char* asset_file){
    char dir_internal[2048],dir_external[2048];
    sprintf(dir_external, "%s/%s", MAIN.InternalDataPath,asset_file);
    sprintf(dir_internal, "%s",asset_file);
    FILE* fo=fopen(dir_external,"rb"); if(fo){ fclose(fo); logPrint("Asset exists %s\n",dir_external); return; }
    FILE* fi=fopen(dir_internal,"rb"); if(!fi){ logPrint("Unable to find asset %s\n",dir_internal); return; }
    fseek(fi,0,SEEK_END); int filesize=ftell(fi); fseek(fi,0,SEEK_SET);
    fo=fopen(dir_external,"wb");
    void* data=malloc(filesize); fread(data,filesize,1,fi);
    fwrite(data,filesize,1,fo); fclose(fi); fclose(fo); free(data);
}
#endif

int ourInit(){
    Our=memAcquire(sizeof(OurPaint));
    MAIN.EnableLogStdOut=1;

    ourRegisterEverything();

    our_InitColorProfiles();

    char error[1024]=""; int status;

    Our->SmudgeTexture=tnsCreate2DTexture(OUR_CANVAS_GL_PIX,256,1,0);

    Our->CanvasShader = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* source1 = OUR_CANVAS_SHADER;
    char* UseContent=tnsEnsureShaderCommoms(source1,0,0);
#ifdef LA_USE_GLES
    const GLchar* versionstr=OUR_SHADER_VERSION_320ES;
#else
    const GLchar* versionstr=OUR_SHADER_VERSION_430;
#endif
    const GLchar* sources1[]={versionstr, UseContent};
    glShaderSource(Our->CanvasShader, 2, sources1, NULL); glCompileShader(Our->CanvasShader);
    glGetShaderiv(Our->CanvasShader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE){
        glGetShaderInfoLog(Our->CanvasShader, sizeof(error), 0, error); logPrintNew("Canvas shader error:\n%s", error); glDeleteShader(Our->CanvasShader); return 0;
    } else {
        glGetShaderInfoLog(Our->CanvasShader, sizeof(error), 0, error); if(error[0]) logPrintNew("Canvas shader info:\n%s", error);
    }
    if(UseContent){ free(UseContent); }

    Our->CanvasProgram = glCreateProgram();
    glAttachShader(Our->CanvasProgram, Our->CanvasShader); glLinkProgram(Our->CanvasProgram);
    glGetProgramiv(Our->CanvasProgram, GL_LINK_STATUS, &status);
    if (status == GL_FALSE){
        glGetProgramInfoLog(Our->CanvasProgram, sizeof(error), 0, error); logPrintNew("Canvas program Linking error:\n%s", error); return 0;
    } else {
        glGetProgramInfoLog(Our->CanvasProgram, sizeof(error), 0, error); if (error[0]) logPrintNew("Canvas program Linking info:\n%s", error);
    }

    Our->CompositionShader = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* source2 = OUR_COMPOSITION_SHADER;
    const GLchar* sources2[]={versionstr, source2};
    glShaderSource(Our->CompositionShader, 2, sources2, NULL); glCompileShader(Our->CompositionShader);
    glGetShaderiv(Our->CompositionShader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE){
        glGetShaderInfoLog(Our->CompositionShader, sizeof(error), 0, error); logPrintNew("Composition shader error:\n%s", error); glDeleteShader(Our->CompositionShader); return 0;
    } else {
        glGetShaderInfoLog(Our->CompositionShader, sizeof(error), 0, error); if(error[0]) logPrintNew("Composition shader info:\n%s", error);
    }

    Our->CompositionProgram = glCreateProgram();
    glAttachShader(Our->CompositionProgram, Our->CompositionShader); glLinkProgram(Our->CompositionProgram);
    glGetProgramiv(Our->CompositionProgram, GL_LINK_STATUS, &status);
    if (status == GL_FALSE){
        glGetProgramInfoLog(Our->CompositionProgram, sizeof(error), 0, error); logPrintNew("Composition program Linking error:\n%s", error); return 0;
    } else {
        glGetProgramInfoLog(Our->CompositionProgram, sizeof(error), 0, error); if(error[0]) logPrintNew("Composition shader info:\n%s", error);
    }

    Our->uCanvasType=glGetUniformLocation(Our->CanvasProgram,"uCanvasType");
    Our->uCanvasRandom=glGetUniformLocation(Our->CanvasProgram,"uCanvasRandom");
    Our->uCanvasFactor=glGetUniformLocation(Our->CanvasProgram,"uCanvasFactor");
    Our->uImageOffset=glGetUniformLocation(Our->CanvasProgram,"uImageOffset");
    Our->uBrushCorner=glGetUniformLocation(Our->CanvasProgram,"uBrushCorner");
    Our->uBrushCenter=glGetUniformLocation(Our->CanvasProgram,"uBrushCenter");
    Our->uBrushSize=glGetUniformLocation(Our->CanvasProgram,"uBrushSize");
    Our->uBrushHardness=glGetUniformLocation(Our->CanvasProgram,"uBrushHardness");
    Our->uBrushSmudge=glGetUniformLocation(Our->CanvasProgram,"uBrushSmudge");
    Our->uBrushRecentness=glGetUniformLocation(Our->CanvasProgram,"uBrushRecentness");
    Our->uBrushColor=glGetUniformLocation(Our->CanvasProgram,"uBrushColor");
    Our->uBrushSlender=glGetUniformLocation(Our->CanvasProgram,"uBrushSlender");
    Our->uBrushAngle=glGetUniformLocation(Our->CanvasProgram,"uBrushAngle");
    Our->uBrushDirection=glGetUniformLocation(Our->CanvasProgram,"uBrushDirection");
    Our->uBrushForce=glGetUniformLocation(Our->CanvasProgram,"uBrushForce");
    Our->uBrushGunkyness=glGetUniformLocation(Our->CanvasProgram,"uBrushGunkyness");
    Our->uBrushErasing=glGetUniformLocation(Our->CanvasProgram,"uBrushErasing");
    Our->uBrushMix=glGetUniformLocation(Our->CanvasProgram,"uBrushMix");

#ifdef LA_USE_GLES
    Our->uBrushRoutineSelectionES=glGetUniformLocation(Our->CanvasProgram, "uBrushRoutineSelectionES");
#else
    Our->uBrushRoutineSelection=glGetSubroutineUniformLocation(Our->CanvasProgram, GL_COMPUTE_SHADER, "uBrushRoutineSelection");
    Our->RoutineDoDabs=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoDabs");
    Our->RoutineDoSample=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoSample");
#endif

#ifdef LA_USE_GLES
    Our->uMixRoutineSelectionES=glGetUniformLocation(Our->CanvasProgram, "uMixRoutineSelectionES");
#else
    Our->uMixRoutineSelection=glGetSubroutineUniformLocation(Our->CanvasProgram, GL_COMPUTE_SHADER, "uMixRoutineSelection");
    Our->RoutineDoMixNormal=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoMixNormal");
    Our->RoutineDoMixSpectral=glGetSubroutineIndex(Our->CanvasProgram, GL_COMPUTE_SHADER, "DoMixSpectral");
#endif

    Our->uBlendMode=glGetUniformLocation(Our->CompositionProgram,"uBlendMode");
    Our->uAlphaTop=glGetUniformLocation(Our->CompositionProgram,"uAlphaTop");
    Our->uAlphaBottom=glGetUniformLocation(Our->CompositionProgram,"uAlphaBottom");


    Our->X=-2800/2; Our->W=2800;
    Our->Y=2400/2;  Our->H=2400;
    Our->BorderAlpha=0.6;

#ifdef LA_USE_GLES
    Our->DefaultScale=1.0;
    Our->SpectralMode=0;
#else
    Our->DefaultScale=0.5;
    Our->SpectralMode=1;
#endif

    Our->BackgroundType=2;
    Our->BackgroundFactor=1;
    srand(time(0));
    Our->BackgroundRandom=rand()-RAND_MAX/2;

    Our->BrushSize=2; Our->BrushBaseSize=2;
    Our->EnableBrushCircle=1;
    Our->PaintUndoLimit=100;

    Our->AllowNonPressure=1;
    Our->BadEventsLimit=7;

    Our->PenID=-1;
    Our->EraserID=-1;
    Our->BrushNumber=3;

    Our->RefAlpha=0.75;
    Our->RefCategory=0;
    Our->RefSize=4;
    tnsVectorSet3(Our->RefMargins,1.5,1.5,1.0);
    tnsVectorSet2(Our->RefPaddings,1.5,1.5);

    tnsEnableShaderv(T->immShader);

    tnsVectorSet3(Our->BackgroundColor,0.2,0.2,0.2);
    our_NewLayer("Our Layer");
    OurBrush* ob=our_NewBrush("Our Brush",0,0.95,9,0.5,0.5,5,0,0,0,0); laset_InstanceUID(ob,"OURBRUSH_Default_Yiming");
    laMarkMemClean(ob); laMarkMemClean(Our->CanvasSaverDummyList.pFirst);

    laAddRootDBInst("our.canvas");

    Our->SplashImage=tnsNewImage(DATA_SPLASH);
    Our->SplashImageHigh=tnsNewImage(DATA_SPLASH_HIGHDPI);

    Our->FileRegistered = our_FileAssociationsRegistered();

    Our->SegmentedWrite = 1;

#ifdef LAGUI_ANDROID
    android_ensure_asset_to_public_dir("default_brushes.udf");
    android_ensure_asset_to_public_dir("default_pallettes.udf");
#endif

    return 1;
}


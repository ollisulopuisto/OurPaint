#include "ourpaint.h"

extern LA MAIN;
extern tnsMain* T;
extern OurPaint *Our;

void CanvasPanel(laUiList *uil, laPropPack *This, laPropPack *DetachedProps, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil);
    
    laUiItem* ui=laShowCanvas(uil,c,0,"our.canvas",0,-1);
}

void* ourget_our(void* unused, void* unused1){
    return Our;
}

void our_CanvasDrawInit(laUiItem* ui){
    la_CanvasInit(ui);

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
void our_CanvasDrawCanvas(laBoxedTheme *bt, OurCanvas *unused_c, laUiItem* ui){
    OurCanvasDraw* ocd=ui->Extra; OurCanvas* oc=ui->PP.EndInstance; laCanvasExtra*e=&ocd->Base;
    int W, H; W = ui->R - ui->L; H = ui->B - ui->U;
    tnsFlush();

    if (!e->OffScr || e->OffScr->pColor[0]->Height != ui->B - ui->U || e->OffScr->pColor[0]->Width != ui->R - ui->L){
        if (e->OffScr) tnsDelete2DOffscreen(e->OffScr);
        e->OffScr = tnsCreate2DOffscreen(GL_RGBA, W, H, 0, 0);
    }

    if(!oc->Content){
        oc->Content=tnsCreate2DTexture(GL_RGBA8,1024,1024,0);
    }

    tnsBindTexture(oc->Content);
    glBindImageTexture(0, oc->Content->GLTexHandle, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);

    glUseProgram(Our->CanvasProgram);
    

    for(int i=0;i<100;i++){
        glUniform2i(Our->CanvasTaskUniform,i,0);
        glDispatchCompute(32, 32, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
    //for(int i=0;i<32;i++){
    //    for(int j=0;j<32;j++){
    //        glUniform2i(Our->CanvasTaskUniform,i,j);
    //        glDispatchCompute(1, 1, 1);
    //        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    //    }
    //}

    tnsEnableShaderv(T->immShader); tnsUseImmShader();
    
    tnsDrawToOffscreen(e->OffScr, 1, 0);
    tnsViewportWithScissor(0, 0, W, H);
    tnsResetViewMatrix();tnsResetModelMatrix();tnsResetProjectionMatrix();
    tnsOrtho(e->PanX - W * e->ZoomX / 2, e->PanX + W * e->ZoomX / 2, e->PanY - e->ZoomY * H / 2, e->PanY + e->ZoomY * H / 2, 100, -100);

    glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT);

    real w2=oc->Content->Width/2, h2=oc->Content->Height/2;
    tnsDraw2DTextureDirectly(oc->Content, -w2, h2, oc->Content->Width, -oc->Content->Height);
    tnsFlush();
}


void ourRegisterEverything(){
    laPropContainer* pc; laKeyMapper* km;

    laRegisterUiTemplate("panel_canvas", "Canvas", CanvasPanel, 0, 0);
    
    pc=laDefineRoot();
    laAddSubGroup(pc,"our","Our","OurPaint main","our_paint",0,0,0,-1,ourget_our,0,0,0,0,0,0,0);

    pc=laAddPropertyContainer("our_paint","Our Paint","OurPaint main",0,0,sizeof(OurPaint),0,0,1);
    laAddSubGroup(pc,"canvas","Canvas","OurPaint canvas","our_canvas",0,0,0,offsetof(OurPaint,Canvas),0,0,0,0,0,0,0,LA_UDF_SINGLE);

    pc=laAddPropertyContainer("our_canvas","Our Canvas","OurPaint canvas",0,0,sizeof(OurCanvas),0,0,1);

    laCanvasTemplate* ct=laRegisterCanvasTemplate("our_CanvasDraw", "our_canvas", 0, our_CanvasDrawCanvas, la_CanvasDrawOverlay, our_CanvasDrawInit, la_CanvasDestroy);
    pc = laCanvasHasExtraProps(ct,sizeof(OurCanvasDraw),2);
    km = &ct->KeyMapper;
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_DOWN, 0, "direction=out");
    laAssignNewKey(km, 0, "LA_2d_view_zoom", LA_KM_SEL_UI_EXTRA, 0, LA_MOUSE_WHEEL_UP, 0, "direction=in");
    laAssignNewKey(km, 0, "LA_2d_view_move", LA_KM_SEL_UI_EXTRA, LA_KEY_ALT, LA_L_MOUSE_DOWN, 0, 0);
    laAssignNewKey(km, 0, "LA_2d_view_click", LA_KM_SEL_UI_EXTRA, 0, LA_L_MOUSE_DOWN, 0, 0);
}

int main(int argc, char *argv[]){
    laGetReady();

    ourInit();

    ourRegisterEverything();

    laRefreshUDFRegistries();
    laEnsureUserPreferences();

    laWindow* w = laDesignWindow(-1,-1,600,600);

    laLayout* l = laDesignLayout(w, "Our Paint");
    laBlock* b = l->FirstBlock;
    laCreatePanel(b, "panel_canvas");

    laStartWindow(w);
    laMainLoop();
}

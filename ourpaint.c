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

extern LA MAIN;
extern tnsMain* T;
extern OurPaint *Our;

#ifdef _WIN32
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#endif

int main(int argc, char *argv[]){
    if(ourProcessInitArgs(argc,argv) < 0){ return 0; }
    laInitArguments ia={0}; laSetDefaultInitArguments(&ia);
    ia.GLMajor=4; ia.GLMinor=5;
    ia.GLESMajor=3; ia.GLESMinor=2;
    ia.UseColorManagement=1;
    ia.HasTextureInspector=1;
    ia.HasTerminal=1;
    ia.HasHistories=1;
    ia.HasHalftoneOption=1;
    //MAIN.EnableGLDebug=1;
    //MAIN.GLDebugSync=1;
    laProcessInitArguments(argc, argv, &ia);
    laGetReadyWith(&ia);

    if(!ourInit()){ laShutoff(0); return -1; }

    laRefreshUDFRegistries();
    laEnsureUserPreferences();

    laLoadHyperResources("OURBRUSH");
    laLoadHyperResources("OURPALLETTE");
    laLoadHyperResources("OURLIGHT");
    laLoadHyperResources("OURSURF");
    laLoadHyperResources("OURPIGM");

    int anyload=0;
    for(int i=1;i<argc;i++){
        char* file=argv[i]; int mode=LA_UDF_MODE_APPEND;
        char* ext=strGetLastSegment(file,'.'); strToLower(ext);
        if(strSame(ext,"ourpaint")){ mode=LA_UDF_MODE_OVERWRITE; }

        laManagedUDF* m; laUDF* udf = laOpenUDF(file, 1, 0, &m);
        if(udf){ laExtractUDF(udf,m,mode); laCloseUDF(udf); anyload=1; }
    }

    if(!MAIN.InputMapping->Toolboxes.pFirst){
        laInputMapping* im=laNewToolbox("Default Toolbox"); laInputMappingEntry* ime;
        ime=laNewInputMappingEntry(im,0,0,0,0,"la.undo"); strSafeSet(&ime->Key,"Undo");
        ime=laNewInputMappingEntry(im,0,0,0,0,"la.redo"); strSafeSet(&ime->Key,"Redo");
        ime=laNewInputMappingEntry(im,0,0,0,0,"la.save"); strSafeSet(&ime->Key,"Save");
        ime=laNewInputMappingEntry(im,0,0,0,0,"our.brush_smaller"); strSafeSet(&ime->Key,"Smaller");
        ime=laNewInputMappingEntry(im,0,0,0,0,"our.brush_bigger"); strSafeSet(&ime->Key,"Bigger");
    }

    if(!MAIN.Windows.pFirst){
        laWindow* w = laDesignWindow(-1,-1,35*LA_RH,25*LA_RH);
        laLayout* l = laDesignLayout(w, "Our Paint");
        laBlock* b = l->FirstBlock;
#ifdef LAGUI_ANDROID
        laSplitBlockVertical(b,0.3);
        laSplitBlockHorizon(b->B1, 0.5);
        laCreatePanel(b->B1->B1, "panel_color");
        laCreatePanel(b->B1->B2, "panel_brushes");
        b->B2->Folded = 1; laCreatePanel(b->B2, "panel_canvas");
        l = laDesignLayout(w, "Our Paint");
        b = l->FirstBlock;
        b->Folded=1; laCreatePanel(b, "panel_canvas");
#else
        laSplitBlockHorizon(b,0.7);
        b->B1->Folded = 1;
        laCreatePanel(b->B1, "panel_canvas");
        laBlock* br=b->B2;
        laSplitBlockVertical(br,0.6);
        laCreatePanel(br->B1, "panel_color");
        laCreatePanel(br->B1, "panel_tools");
        laCreatePanel(br->B1, "panel_brushes");
        laCreatePanel(br->B2, "panel_notes");
        laCreatePanel(br->B2, "panel_layers");
        laStartWindow(w);
#endif
    }

    ourFinalize(anyload);

    if(!anyload) our_EnableSplashPanel();

    laMainLoop();
}

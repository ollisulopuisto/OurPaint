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

int main(int argc, char *argv[]){
    laGetReadyWith(4,5,0);

    if(!ourInit()){ laShutoff(0); return -1; }

    laRefreshUDFRegistries();
    laEnsureUserPreferences();

    laLoadHyperResources("OURBRUSH");

    for(int i=1;i<argc;i++){
        char* file=argv[i]; 
        laManagedUDF* m; laUDF* udf = laOpenUDF(file, 1, 0, &m);
        if(udf){ laExtractUDF(udf,m,LA_UDF_MODE_APPEND,0); laCloseUDF(udf); }
    }

    //laAddRootDBInst("la.input_mapping");
    //laAddRootDBInst("la.drivers");
    //laAddRootDBInst("our.tools");
    if(!MAIN.Windows.pFirst){
        laWindow* w = laDesignWindow(-1,-1,35*LA_RH,25*LA_RH);
        laLayout* l = laDesignLayout(w, "Our Paint");
        laBlock* b = l->FirstBlock;
        laSplitBlockHorizon(b,0.7);
        laCreatePanel(b->B1, "panel_canvas");
        laBlock* br=b->B2;
        laSplitBlockVertical(br,0.6);
        laCreatePanel(br->B1, "panel_color");
        laCreatePanel(br->B1, "panel_tools");
        laCreatePanel(br->B1, "panel_brushes");
        laCreatePanel(br->B2, "panel_layers");
        laStartWindow(w);
    }
    our_EnableSplashPanel();
    laMainLoop();
}

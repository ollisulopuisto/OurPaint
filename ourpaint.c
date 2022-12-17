#include "ourpaint.h"

extern LA MAIN;
extern tnsMain* T;
extern OurPaint *Our;

int main(int argc, char *argv[]){
    laGetReady();

    if(!ourInit()){ laShutoff(); return -1; }

    laRefreshUDFRegistries();
    laEnsureUserPreferences();

    for(int i=1;i<argc;i++){
        char* file=argv[i]; 
        laManagedUDF* m; laUDF* udf = laOpenUDF(file, 1, 0, &m);
        if(udf){ laExtractUDF(udf,m,LA_UDF_MODE_APPEND,0); laCloseUDF(udf); }
    }

    //laAddRootDBInst("la.input_mapping");
    //laAddRootDBInst("la.drivers");
    //laAddRootDBInst("our.tools");

    laWindow* w = laDesignWindow(-1,-1,600,600);

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
    laMainLoop();
}

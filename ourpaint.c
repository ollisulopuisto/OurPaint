#include "ourpaint.h"

extern LA MAIN;
extern tnsMain* T;
extern OurPaint *Our;

int main(int argc, char *argv[]){
    laGetReady();

    ourInit();

    laRefreshUDFRegistries();
    laEnsureUserPreferences();

    laWindow* w = laDesignWindow(-1,-1,600,600);

    laLayout* l = laDesignLayout(w, "Our Paint");
    laBlock* b = l->FirstBlock;
    laSplitBlockHorizon(b,0.7);
    laCreatePanel(b->B1, "panel_canvas");
    laBlock* br=b->B2;
    laSplitBlockVertical(br,0.6);
    laCreatePanel(br->B1, "panel_color");
    laCreatePanel(br->B1, "panel_brushes");
    laCreatePanel(br->B2, "panel_layers");

    laStartWindow(w);
    laMainLoop();
}

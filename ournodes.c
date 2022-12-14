#include "ourpaint.h"

extern LA MAIN;
extern tnsMain* T;
extern OurPaint *Our;

laBaseNodeType OUR_IDN_BRUSH_SETTINGS;
laBaseNodeType OUR_IDN_BRUSH_OUTPUTS;
laBaseNodeType OUR_IDN_BRUSH_DEVICE;

laPropContainer* OUR_PC_IDN_BRUSH_SETTINGS;
laPropContainer* OUR_PC_IDN_BRUSH_OUTPUTS;
laPropContainer* OUR_PC_IDN_BRUSH_DEVICE;

void IDN_BrushSettingsInit(OurBrushSettingsNode* n){
    n->CanvasScale=laCreateOutSocket(n,"Canvas Scale",LA_PROP_FLOAT);   n->CanvasScale->Data=&n->rCanvasScale;
    n->Size=laCreateOutSocket(n,"Size",LA_PROP_FLOAT);                  n->Size->Data=&n->rSize;
    n->Transparency=laCreateOutSocket(n,"Transparency",LA_PROP_FLOAT);  n->Transparency->Data=&n->rTransparency;
    n->Hardness=laCreateOutSocket(n,"Hardness",LA_PROP_FLOAT);          n->Hardness->Data=&n->rHardness;
    n->Smudge=laCreateOutSocket(n,"Smudge",LA_PROP_FLOAT);              n->Smudge->Data=&n->rSmudge;
    n->SmudgeLength=laCreateOutSocket(n,"Smudge Length",LA_PROP_FLOAT); n->SmudgeLength->Data=&n->rSmudgeLength;
    n->DabsPerSize=laCreateOutSocket(n,"Dabs Per Size",LA_PROP_FLOAT);  n->DabsPerSize->Data=&n->rDabsPerSize;
    n->Slender=laCreateOutSocket(n,"Slender",LA_PROP_FLOAT);            n->Slender->Data=&n->rSlender;
    n->Angle=laCreateOutSocket(n,"Angle",LA_PROP_FLOAT);                n->Angle->Data=&n->rAngle;
    strSafeSet(&n->Base.Name, "Brush Settings");
}
void IDN_BrushSettingsDestroy(OurBrushSettingsNode* n){
    laDestroyOutSocket(n->Size); laDestroyOutSocket(n->Transparency); laDestroyOutSocket(n->Hardness); laDestroyOutSocket(n->Smudge);
    laDestroyOutSocket(n->SmudgeLength); laDestroyOutSocket(n->DabsPerSize); laDestroyOutSocket(n->Slender); laDestroyOutSocket(n->Angle);
    laDestroyOutSocket(n->CanvasScale); strSafeDestroy(&n->Base.Name);
}
int IDN_BrushSettingsVisit(OurBrushSettingsNode* n, laListHandle* l){
    LA_GUARD_THIS_NODE(n); n->Base.Eval=LA_DAG_FLAG_PERM; lstAppendPointer(l, n);
    return LA_DAG_FLAG_PERM;
}
int IDN_BrushSettingsEval(OurBrushSettingsNode* n){
    if(!Our->CurrentBrush){ return 0; } // unlikely;
    n->rCanvasScale = Our->CurrentScale;
    n->rSize = Our->CurrentBrush->Size;
    n->rTransparency = Our->CurrentBrush->Transparency;
    n->rHardness = Our->CurrentBrush->Hardness;
    n->rSmudge = Our->CurrentBrush->Smudge;
    n->rSmudgeLength = Our->CurrentBrush->SmudgeResampleLength;
    n->rDabsPerSize = Our->CurrentBrush->DabsPerSize;
    n->rSlender = Our->CurrentBrush->Slender;
    n->rAngle = Our->CurrentBrush->Angle;
    return 1;
}
void ui_BrushSettingsNode(laUiList *uil, laPropPack *This, laPropPack *Extra, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); OurBrushSettingsNode*n=This->EndInstance;
    laUiItem* b,*u;
    LA_BASE_NODE_HEADER(uil,c,This);
    
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Canvas Scale",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;  laShowNodeSocket(uil,c,This,"canvas_scale",0);  laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Size",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;          laShowNodeSocket(uil,c,This,"size",0);          laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Transparency",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;  laShowNodeSocket(uil,c,This,"transparency",0);  laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Hardness",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;      laShowNodeSocket(uil,c,This,"hardness",0);      laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Smudge",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;        laShowNodeSocket(uil,c,This,"smudge",0);        laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Smudge Length",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1; laShowNodeSocket(uil,c,This,"smudge_length",0); laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Dabs Per Size",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1; laShowNodeSocket(uil,c,This,"dabs_per_size",0); laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Slender",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;       laShowNodeSocket(uil,c,This,"slender",0);       laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Angle",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;         laShowNodeSocket(uil,c,This,"angle",0);         laEndRow(uil,b);
}

void IDN_BrushOutputsInit(OurBrushOutputsNode* n){
    n->Position=laCreateInSocket("Position",LA_PROP_FLOAT);
    n->Size=laCreateInSocket("Size",LA_PROP_FLOAT);
    n->Transparency=laCreateInSocket("Transparency",LA_PROP_FLOAT);
    n->Hardness=laCreateInSocket("Hardness",LA_PROP_FLOAT);
    n->Smudge=laCreateInSocket("Smudge",LA_PROP_FLOAT);
    n->SmudgeLength=laCreateInSocket("Smudge Length",LA_PROP_FLOAT);
    n->DabsPerSize=laCreateInSocket("Dabs Per Size",LA_PROP_FLOAT);
    n->Slender=laCreateInSocket("Slender",LA_PROP_FLOAT);
    n->Angle=laCreateInSocket("Angle",LA_PROP_FLOAT);
    strSafeSet(&n->Base.Name, "Brush Outputs");
}
void IDN_BrushOutputsDestroy(OurBrushOutputsNode* n){
    laDestroyInSocket(n->Position);
    laDestroyInSocket(n->Size); laDestroyInSocket(n->Transparency); laDestroyInSocket(n->Hardness); laDestroyInSocket(n->Smudge);
    laDestroyInSocket(n->SmudgeLength); laDestroyInSocket(n->DabsPerSize); laDestroyInSocket(n->Slender); laDestroyInSocket(n->Angle);
    strSafeDestroy(&n->Base.Name);
}
int IDN_BrushOutputsVisit(OurBrushOutputsNode* n, laListHandle* l){
    LA_GUARD_THIS_NODE(n);
#define BRUSH_OUT_VISIT(a)\
    if(LA_SRC_AND_PARENT(n->a)){ laBaseNode*bn=n->a->Source->Parent; LA_VISIT_NODE(bn); }
    BRUSH_OUT_VISIT(Position)
    BRUSH_OUT_VISIT(Size)
    BRUSH_OUT_VISIT(Transparency)
    BRUSH_OUT_VISIT(Hardness)
    BRUSH_OUT_VISIT(Smudge)
    BRUSH_OUT_VISIT(SmudgeLength)
    BRUSH_OUT_VISIT(DabsPerSize)
    BRUSH_OUT_VISIT(Slender)
    BRUSH_OUT_VISIT(Angle)
#undef BRUSH_OUT_VISIT
    n->Base.Eval=LA_DAG_FLAG_PERM; lstAppendPointer(l, n);
    return LA_DAG_FLAG_PERM;
}
int IDN_BrushOutputsEval(OurBrushOutputsNode* n){
    if(!Our->CurrentBrush) return 0;
#define BRUSH_OUT_EVAL(a)\
    if(LA_SRC_AND_PARENT(n->a) && (n->a->Source->DataType&LA_PROP_FLOAT)){ Our->CurrentBrush->Eval##a=*((real*)n->a->Source->Data); }
    if(LA_SRC_AND_PARENT(n->Position) && (n->Position->Source->DataType&LA_PROP_FLOAT|LA_PROP_ARRAY) && n->Position->Source->ArrLen>=2){
        Our->CurrentBrush->EvalPositionOut[0]=((real*)n->Position->Source->Data)[0];
        Our->CurrentBrush->EvalPositionOut[1]=((real*)n->Position->Source->Data)[1];
    }
    BRUSH_OUT_EVAL(Size)
    BRUSH_OUT_EVAL(Transparency)
    BRUSH_OUT_EVAL(Hardness)
    BRUSH_OUT_EVAL(Smudge)
    BRUSH_OUT_EVAL(SmudgeLength)
    BRUSH_OUT_EVAL(DabsPerSize)
    BRUSH_OUT_EVAL(Slender)
    BRUSH_OUT_EVAL(Angle)
#undef BRUSH_OUT_EVAL
    return 1;
}
void ui_BrushOutputsNode(laUiList *uil, laPropPack *This, laPropPack *Extra, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); OurBrushOutputsNode*n=This->EndInstance;
    laUiItem* b,*u;
    LA_BASE_NODE_HEADER(uil,c,This);

    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"position",0);      laShowLabel(uil,c,"Position",0,0);      laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"size",0);          laShowLabel(uil,c,"Size",0,0);          laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"transparency",0);  laShowLabel(uil,c,"Transparency",0,0);  laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"hardness",0);      laShowLabel(uil,c,"Hardness",0,0);      laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"smudge",0);        laShowLabel(uil,c,"Smudge",0,0);        laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"smudge_length",0); laShowLabel(uil,c,"Smudge Length",0,0); laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"dabs_per_size",0); laShowLabel(uil,c,"Dabs Per Size",0,0); laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"slender",0);       laShowLabel(uil,c,"Slender",0,0);       laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"angle",0);         laShowLabel(uil,c,"Angle",0,0);         laEndRow(uil,b);
}

void IDN_BrushDeviceInit(OurBrushDeviceNode* n){
    n->Pressure=laCreateOutSocket(n,"Pressure",LA_PROP_FLOAT);               n->Pressure->Data=&n->rPressure;
    n->Position=laCreateOutSocket(n,"Position",LA_PROP_FLOAT|LA_PROP_ARRAY); n->Position->Data=n->rPosition; n->Position->ArrLen=2;
    n->Tilt=laCreateOutSocket(n,"Tilt",LA_PROP_FLOAT|LA_PROP_ARRAY);         n->Tilt->Data=n->rTilt; n->Tilt->ArrLen=2;
    n->IsEraser=laCreateOutSocket(n,"Is Eraser",LA_PROP_INT);                n->IsEraser->Data=&n->rIsEraser;
    n->LastPosition=laCreateOutSocket(n,"Last Position",LA_PROP_FLOAT|LA_PROP_ARRAY); n->LastPosition->Data=n->rLastPosition; n->LastPosition->ArrLen=2;
    strSafeSet(&n->Base.Name, "Brush Device");
}
void IDN_BrushDeviceDestroy(OurBrushDeviceNode* n){
    laDestroyOutSocket(n->Pressure); laDestroyOutSocket(n->Tilt); laDestroyOutSocket(n->Position); laDestroyOutSocket(n->IsEraser); laDestroyOutSocket(n->LastPosition);
    strSafeDestroy(&n->Base.Name);
}
int IDN_BrushDeviceVisit(OurBrushDeviceNode* n, laListHandle* l){
    LA_GUARD_THIS_NODE(n); n->Base.Eval=LA_DAG_FLAG_PERM; lstAppendPointer(l, n);
    return LA_DAG_FLAG_PERM;
}
int IDN_BrushDeviceEval(OurBrushDeviceNode* n){
    if(!Our->CurrentBrush){ return 0; } // unlikely;
    tnsVectorSet2v(n->rPosition, Our->CurrentBrush->EvalPosition);
    tnsVectorSet2(n->rLastPosition, Our->CurrentBrush->LastX, Our->CurrentBrush->LastY); printf("%lf %lf\n",Our->CurrentBrush->LastX, Our->CurrentBrush->LastY);
    tnsVectorSet2v(n->rTilt, Our->CurrentBrush->EvalTilt);
    n->rIsEraser = Our->CurrentBrush->EvalIsEraser;
    n->rPressure = Our->CurrentBrush->EvalPressure;
    return 1;
}
void ui_BrushDeviceNode(laUiList *uil, laPropPack *This, laPropPack *Extra, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); OurBrushDeviceNode*n=This->EndInstance;
    laUiItem* b,*u;
    LA_BASE_NODE_HEADER(uil,c,This);

    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Position",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1; laShowNodeSocket(uil,c,This,"position",0);  laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Last Position",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1; laShowNodeSocket(uil,c,This,"last_position",0);  laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Pressure",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1; laShowNodeSocket(uil,c,This,"pressure",0);  laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Tilt",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;     laShowNodeSocket(uil,c,This,"tilt",0);      laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Is Eraser",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;laShowNodeSocket(uil,c,This,"is_eraser",0); laEndRow(uil,b);

}

int ourEvalBrush(){
    for(laListItemPointer*lip=Our->BrushEval.pFirst;lip;lip=lip->pNext){ laBaseNode* n=lip->p; n->Type->Eval(n); }
    return 1;
}
int ourRebuildBrushEval(){
    while(lstPopPointer(&Our->BrushEval));
    if(!Our->CurrentBrush || !Our->CurrentBrush->Rack) return LA_DAG_FLAG_PERM;
    laListHandle pending={0}; laRackPage* rp=Our->CurrentBrush->Rack; if(!rp)return LA_DAG_FLAG_PERM;
    for(laNodeRack* ir=rp->Racks.pFirst;ir;ir=ir->Item.pNext){
        for(laBaseNode*bn=ir->Nodes.pFirst;bn;bn=bn->Item.pNext){ lstAppendPointer(&pending,bn); bn->Eval=0; }
    }
    laBaseNode*n;int result=LA_DAG_FLAG_PERM; laListItemPointer*NextLip;
    for(laListItemPointer*lip=pending.pFirst;lip;lip=NextLip){ n=lip->p; NextLip=lip->pNext;
        if(n->Eval&LA_DAG_FLAG_PERM) continue;
        result=n->Type->Visit(n,&Our->BrushEval); if(result==LA_DAG_FLAG_ERR){ while(lstPopPointer(&pending)); break; }
    }
    if(result==LA_DAG_FLAG_ERR){ while(lstPopPointer(&MAIN.InputMapping->Eval)); return LA_DAG_FLAG_ERR; }
    return LA_DAG_FLAG_PERM;
}


void ourRegisterNodes(){
    laPropContainer *pc; laProp *p;
    laOperatorType *at;
    laEnumProp *ep;

    pc=laAddPropertyContainer("our_node_brush_settings", "Brush Settings", "Brush settings node to read from",0,ui_BrushSettingsNode,sizeof(OurBrushSettingsNode),0,0,1);
    OUR_PC_IDN_BRUSH_SETTINGS=pc; laPropContainerExtraFunctions(pc,0,0,0,0,laui_DefaultNodeOperationsPropUiDefine);
    laAddSubGroup(pc,"base","Base","Base node","la_base_node",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"canvas_scale", "Canvas scale","Canvas scale","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,CanvasScale),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"size", "Size","Size","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,Size),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"transparency", "Transparency","Transparency","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,Transparency),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"hardness", "Hardness","Hardness","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,Hardness),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"smudge", "Smudge","Smudge","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,Smudge),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"smudge_length", "Smudge Length","Smudge length","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,SmudgeLength),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"dabs_per_size", "Dabs Per Size","Dabs per size","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,DabsPerSize),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"slender", "Slender","Slender","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,Slender),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"angle", "Angle","Angle","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,Angle),0,0,0,0,0,0,0,LA_UDF_SINGLE);

    pc=laAddPropertyContainer("our_node_brush_outputs", "Brush Outputs", "Brush outputs to draw actual dabs",0,ui_BrushOutputsNode,sizeof(OurBrushOutputsNode),0,0,1);
    OUR_PC_IDN_BRUSH_OUTPUTS=pc; laPropContainerExtraFunctions(pc,0,0,0,0,laui_DefaultNodeOperationsPropUiDefine);
    laAddSubGroup(pc,"base","Base","Base node","la_base_node",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"position", "Position","Position","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Position),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"size", "Size","Size","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Size),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"transparency", "Transparency","Transparency","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Transparency),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"hardness", "Hardness","Hardness","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Hardness),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"smudge", "Smudge","Smudge","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Smudge),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"smudge_length", "Smudge Length","Smudge length","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,SmudgeLength),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"dabs_per_size", "Dabs Per Size","Dabs per size","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,DabsPerSize),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"slender", "Slender","Slender","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Slender),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"angle", "Angle","Angle","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Angle),0,0,0,0,0,0,0,LA_UDF_SINGLE);

    pc=laAddPropertyContainer("our_node_brush_device", "Brush Device", "Brush device input",0,ui_BrushDeviceNode,sizeof(OurBrushDeviceNode),0,0,1);
    OUR_PC_IDN_BRUSH_DEVICE =pc; laPropContainerExtraFunctions(pc,0,0,0,0,laui_DefaultNodeOperationsPropUiDefine);
    laAddSubGroup(pc,"base","Base","Base node","la_base_node",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"pressure","Pressure","Pressure of the input","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Pressure),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"tilt", "Tilt","Pen tilt vector","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Tilt),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"position", "Dab position","Interpolated dab position","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Position),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"last_position", "Last position","Position of the previous dab","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,LastPosition),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"is_eraser", "Is Eraser","Input event is from an eraser","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,IsEraser),0,0,0,0,0,0,0,LA_UDF_SINGLE);

    LA_IDN_REGISTER("Brush Settings",L'🖌',OUR_IDN_BRUSH_SETTINGS,OUR_PC_IDN_BRUSH_SETTINGS, IDN_BrushSettingsInit, IDN_BrushSettingsDestroy, IDN_BrushSettingsVisit, IDN_BrushSettingsEval, OurBrushSettingsNode);
    LA_IDN_REGISTER("Brush Outputs",L'🖌',OUR_IDN_BRUSH_OUTPUTS,OUR_PC_IDN_BRUSH_OUTPUTS, IDN_BrushOutputsInit, IDN_BrushOutputsDestroy, IDN_BrushOutputsVisit, IDN_BrushOutputsEval, OurBrushOutputsNode);
    LA_IDN_REGISTER("Brush Device",L'🖳',OUR_IDN_BRUSH_DEVICE,OUR_PC_IDN_BRUSH_DEVICE, IDN_BrushDeviceInit, IDN_BrushDeviceDestroy, IDN_BrushDeviceVisit, IDN_BrushDeviceEval, OurBrushDeviceNode);
    
    laNodeCategory* nc=laAddNodeCategory("Our Paint",0,LA_RACK_TYPE_DRIVER);

    laNodeCategoryAddNodeTypes(LA_NODE_CATEGORY_DRIVER, &OUR_IDN_BRUSH_OUTPUTS,0);
    laNodeCategoryAddNodeTypes(nc, &OUR_IDN_BRUSH_DEVICE, 0);
    laNodeCategoryAddNodeTypes(nc, &OUR_IDN_BRUSH_SETTINGS, 0);
}


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

void IDN_BrushSettingsInit(OurBrushSettingsNode* n, int NoCreate){
    if(!NoCreate){
        n->CanvasScale=laCreateOutSocket(n,"Canvas Scale",LA_PROP_FLOAT);
        n->Size=laCreateOutSocket(n,"SIZE",LA_PROP_FLOAT);
        n->Transparency=laCreateOutSocket(n,"TRANSP",LA_PROP_FLOAT);
        n->Hardness=laCreateOutSocket(n,"HARD",LA_PROP_FLOAT);
        n->Smudge=laCreateOutSocket(n,"SMUDGE",LA_PROP_FLOAT);
        n->SmudgeLength=laCreateOutSocket(n,"LEN",LA_PROP_FLOAT);
        n->DabsPerSize=laCreateOutSocket(n,"Dabs Per Size",LA_PROP_FLOAT);
        n->Slender=laCreateOutSocket(n,"SLENDER",LA_PROP_FLOAT);
        n->Angle=laCreateOutSocket(n,"ANGLE",LA_PROP_FLOAT);
        n->Color=laCreateOutSocket(n,"COLOR",LA_PROP_FLOAT|LA_PROP_ARRAY);
        strSafeSet(&n->Base.Name, "Brush Settings");
    }
    n->CanvasScale->Data=&n->rCanvasScale;
    n->Size->Data=&n->rSize;
    n->Transparency->Data=&n->rTransparency;
    n->Hardness->Data=&n->rHardness;
    n->Smudge->Data=&n->rSmudge;
    n->SmudgeLength->Data=&n->rSmudgeLength;
    n->DabsPerSize->Data=&n->rDabsPerSize;
    n->Slender->Data=&n->rSlender;
    n->Angle->Data=&n->rAngle;
    n->Color->Data=Our->CurrentColor; n->Color->ArrLen=3;
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

    b=laBeginRow(uil,c,0,0);
        laShowSeparator(uil,c)->Expand=1;
        laShowNodeSocket(uil,c,This,"size",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"transparency",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"hardness",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"slender",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"angle",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"color",0)->Flags|=LA_UI_SOCKET_LABEL_N;
    laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0);
        laShowSeparator(uil,c)->Expand=1;
        laShowNodeSocket(uil,c,This,"smudge_length",0)->Flags|=LA_UI_SOCKET_LABEL_W;
        laShowNodeSocket(uil,c,This,"smudge",0)->Flags|=LA_UI_SOCKET_LABEL_W;
    laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); u=laShowLabel(uil,c,"Canvas Scale",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1;  laShowNodeSocket(uil,c,This,"canvas_scale",0);
        u=laShowLabel(uil,c,"Dabs Per Size",0,0);u->Flags|=LA_TEXT_ALIGN_RIGHT; u->Expand=1; laShowNodeSocket(uil,c,This,"dabs_per_size",0); laEndRow(uil,b);
}

void IDN_BrushOutputsInit(OurBrushOutputsNode* n, int NoCreate){
    if(NoCreate){ return; }
    n->Offset=laCreateInSocket("OFFSET",LA_PROP_FLOAT);
    n->Size=laCreateInSocket("SIZE",LA_PROP_FLOAT);
    n->Transparency=laCreateInSocket("TRANSP",LA_PROP_FLOAT);
    n->Hardness=laCreateInSocket("HRAD",LA_PROP_FLOAT);
    n->Smudge=laCreateInSocket("SMUDGE",LA_PROP_FLOAT);
    n->SmudgeLength=laCreateInSocket("LENGTH",LA_PROP_FLOAT);
    n->DabsPerSize=laCreateInSocket("Dabs Per Size",LA_PROP_FLOAT);
    n->Slender=laCreateInSocket("SLENDER",LA_PROP_FLOAT);
    n->Angle=laCreateInSocket("ANGLE",LA_PROP_FLOAT);
    n->Color=laCreateInSocket("COLOR",LA_PROP_FLOAT);
    strSafeSet(&n->Base.Name, "Brush Outputs");
}
void IDN_BrushOutputsDestroy(OurBrushOutputsNode* n){
    laDestroyInSocket(n->Offset);
    laDestroyInSocket(n->Size); laDestroyInSocket(n->Transparency); laDestroyInSocket(n->Hardness); laDestroyInSocket(n->Smudge);
    laDestroyInSocket(n->SmudgeLength); laDestroyInSocket(n->DabsPerSize); laDestroyInSocket(n->Slender); laDestroyInSocket(n->Angle);
    laDestroyInSocket(n->Color);
    strSafeDestroy(&n->Base.Name);
}
int IDN_BrushOutputsVisit(OurBrushOutputsNode* n, laListHandle* l){
    LA_GUARD_THIS_NODE(n);
#define BRUSH_OUT_VISIT(a)\
    if(LA_SRC_AND_PARENT(n->a)){ laBaseNode*bn=n->a->Source->Parent; LA_VISIT_NODE(bn); }
    BRUSH_OUT_VISIT(Offset)
    BRUSH_OUT_VISIT(Size)
    BRUSH_OUT_VISIT(Transparency)
    BRUSH_OUT_VISIT(Hardness)
    BRUSH_OUT_VISIT(Smudge)
    BRUSH_OUT_VISIT(SmudgeLength)
    BRUSH_OUT_VISIT(DabsPerSize)
    BRUSH_OUT_VISIT(Slender)
    BRUSH_OUT_VISIT(Angle)
    BRUSH_OUT_VISIT(Color)
#undef BRUSH_OUT_VISIT
    n->Base.Eval=LA_DAG_FLAG_PERM; lstAppendPointer(l, n);
    return LA_DAG_FLAG_PERM;
}
int IDN_BrushOutputsEval(OurBrushOutputsNode* n){
    if(!Our->CurrentBrush) return 0;
#define BRUSH_OUT_EVAL(a)\
    if(LA_SRC_AND_PARENT(n->a) && (n->a->Source->DataType&LA_PROP_FLOAT)){ Our->CurrentBrush->Eval##a=*((real*)n->a->Source->Data); }
    if(LA_SRC_AND_PARENT(n->Offset) && (n->Offset->Source->DataType&LA_PROP_FLOAT|LA_PROP_ARRAY) && n->Offset->Source->ArrLen>=2){
        Our->CurrentBrush->EvalOffset[0]=((real*)n->Offset->Source->Data)[0];
        Our->CurrentBrush->EvalOffset[1]=((real*)n->Offset->Source->Data)[1];
    }
    if(LA_SRC_AND_PARENT(n->Color) && (n->Color->Source->DataType&LA_PROP_FLOAT|LA_PROP_ARRAY) && n->Color->Source->ArrLen>=3){
        Our->CurrentBrush->EvalColor[0]=((real*)n->Color->Source->Data)[0];
        Our->CurrentBrush->EvalColor[1]=((real*)n->Color->Source->Data)[1];
        Our->CurrentBrush->EvalColor[2]=((real*)n->Color->Source->Data)[2];
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

    b=laBeginRow(uil,c,0,0);
        laShowNodeSocket(uil,c,This,"offset",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"size",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"transparency",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"hardness",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"slender",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"angle",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"color",0)->Flags|=LA_UI_SOCKET_LABEL_N;
    laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0);
        laShowNodeSocket(uil,c,This,"smudge",0)->Flags|=LA_UI_SOCKET_LABEL_E;
        laShowNodeSocket(uil,c,This,"smudge_length",0)->Flags|=LA_UI_SOCKET_LABEL_E;
    laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowNodeSocket(uil,c,This,"dabs_per_size",0); laShowLabel(uil,c,"Dabs Per Size",0,0); laEndRow(uil,b);
}

void IDN_BrushDeviceInit(OurBrushDeviceNode* n, int NoCreate){
    if(!NoCreate){
        n->Pressure=laCreateOutSocket(n,"PRESSURE",LA_PROP_FLOAT);
        n->Tilt=laCreateOutSocket(n,"TILT",LA_PROP_FLOAT|LA_PROP_ARRAY);
        n->IsEraser=laCreateOutSocket(n,"ERASER",LA_PROP_INT);
        n->Position=laCreateOutSocket(n,"POS",LA_PROP_FLOAT|LA_PROP_ARRAY);
        n->Speed=laCreateOutSocket(n,"SPD",LA_PROP_FLOAT);
        n->Angle=laCreateOutSocket(n,"ANGLE",LA_PROP_FLOAT);
        n->Length=laCreateOutSocket(n,"LENGTH",LA_PROP_FLOAT);
        n->LengthAccum=laCreateOutSocket(n,"ACUM",LA_PROP_FLOAT);
        strSafeSet(&n->Base.Name, "Brush Device");
    }
    n->Pressure->Data=&n->rPressure;
    n->Tilt->Data=n->rTilt; n->Tilt->ArrLen=2;
    n->IsEraser->Data=&n->rIsEraser;
    n->Position->Data=n->rPosition; n->Position->ArrLen=2;
    n->Speed->Data=&n->rSpeed;
    n->Angle->Data=&n->rAngle;
    n->Length->Data=&n->rLength;
    n->LengthAccum->Data=&n->rLengthAccum;
}
void IDN_BrushDeviceDestroy(OurBrushDeviceNode* n){
    laDestroyOutSocket(n->Pressure); laDestroyOutSocket(n->Tilt); laDestroyOutSocket(n->Position); laDestroyOutSocket(n->IsEraser); laDestroyOutSocket(n->Speed);
    laDestroyOutSocket(n->Angle); laDestroyOutSocket(n->Length); laDestroyOutSocket(n->LengthAccum); 
    strSafeDestroy(&n->Base.Name);
}
int IDN_BrushDeviceVisit(OurBrushDeviceNode* n, laListHandle* l){
    LA_GUARD_THIS_NODE(n); n->Base.Eval=LA_DAG_FLAG_PERM; lstAppendPointer(l, n);
    return LA_DAG_FLAG_PERM;
}
int IDN_BrushDeviceEval(OurBrushDeviceNode* n){
    if(!Our->CurrentBrush){ return 0; } // unlikely;
    tnsVectorSet2v(n->rPosition, Our->CurrentBrush->EvalPosition);
    tnsVectorSet2v(n->rTilt, Our->CurrentBrush->EvalTilt);
    n->rAngle=Our->CurrentBrush->EvalStrokeAngle;
    n->rIsEraser = Our->CurrentBrush->EvalIsEraser;
    n->rPressure = Our->CurrentBrush->EvalPressure;
    n->rSpeed = Our->CurrentBrush->EvalSpeed;
    n->rLength = Our->CurrentBrush->EvalStrokeLength;
    n->rLengthAccum = Our->CurrentBrush->EvalStrokeLengthAccum;
    return 1;
}
void ui_BrushDeviceNode(laUiList *uil, laPropPack *This, laPropPack *Extra, laColumn *UNUSED, int context){
    laColumn* c=laFirstColumn(uil); OurBrushDeviceNode*n=This->EndInstance;
    laUiItem* b,*u;
    LA_BASE_NODE_HEADER(uil,c,This);

    b=laBeginRow(uil,c,0,0);
        laShowSeparator(uil,c)->Expand=1;
        laShowNodeSocket(uil,c,This,"position",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"speed",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"angle",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"pressure",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"tilt",0)->Flags|=LA_UI_SOCKET_LABEL_N;
        laShowNodeSocket(uil,c,This,"is_eraser",0)->Flags|=LA_UI_SOCKET_LABEL_N;
    laEndRow(uil,b);
    b=laBeginRow(uil,c,0,0); laShowSeparator(uil,c)->Expand=1;
        laShowNodeSocket(uil,c,This,"length_accum",0)->Flags|=LA_UI_SOCKET_LABEL_W; laShowNodeSocket(uil,c,This,"length",0)->Flags|=LA_UI_SOCKET_LABEL_W; laEndRow(uil,b);
    
}

int ourEvalBrush(){
    for(laListItemPointer*lip=Our->BrushEval.pFirst;lip;lip=lip->pNext){
        laBaseNode* n=lip->p; if(!n->InitDone){ n->Type->Init(n,1); n->InitDone=1; } n->Type->Eval(n); 
    }
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
    laAddSubGroup(pc,"color", "Color","Color","la_out_socket",0,0,0,offsetof(OurBrushSettingsNode,Color),0,0,0,0,0,0,0,LA_UDF_SINGLE);

    pc=laAddPropertyContainer("our_node_brush_outputs", "Brush Outputs", "Brush outputs to draw actual dabs",0,ui_BrushOutputsNode,sizeof(OurBrushOutputsNode),0,0,1);
    OUR_PC_IDN_BRUSH_OUTPUTS=pc; laPropContainerExtraFunctions(pc,0,0,0,0,laui_DefaultNodeOperationsPropUiDefine);
    laAddSubGroup(pc,"base","Base","Base node","la_base_node",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"offset", "Offset","Offset","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Offset),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"size", "Size","Size","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Size),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"transparency", "Transparency","Transparency","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Transparency),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"hardness", "Hardness","Hardness","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Hardness),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"smudge", "Smudge","Smudge","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Smudge),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"smudge_length", "Smudge Length","Smudge length","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,SmudgeLength),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"dabs_per_size", "Dabs Per Size","Dabs per size","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,DabsPerSize),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"slender", "Slender","Slender","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Slender),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"angle", "Angle","Angle","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Angle),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"color", "Color","Color","la_in_socket",0,0,0,offsetof(OurBrushOutputsNode,Color),0,0,0,0,0,0,0,LA_UDF_SINGLE);

    pc=laAddPropertyContainer("our_node_brush_device", "Brush Device", "Brush device input",0,ui_BrushDeviceNode,sizeof(OurBrushDeviceNode),0,0,1);
    OUR_PC_IDN_BRUSH_DEVICE =pc; laPropContainerExtraFunctions(pc,0,0,0,0,laui_DefaultNodeOperationsPropUiDefine);
    laAddSubGroup(pc,"base","Base","Base node","la_base_node",0,0,0,0,0,0,0,0,0,0,0,LA_UDF_LOCAL);
    laAddSubGroup(pc,"pressure","Pressure","Pressure of the input","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Pressure),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"tilt", "Tilt","Pen tilt vector","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Tilt),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"is_eraser", "Is Eraser","Input event is from an eraser","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,IsEraser),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"position", "Dab position","Interpolated dab position","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Position),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"speed","Speed","Speed on the canvas","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Speed),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"angle","Angle","Direction of the brush","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Angle),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"length","Length","Length of this brush stroke","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,Length),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    laAddSubGroup(pc,"length_accum","Accumulated Length","Accumulated stroke length","la_out_socket",0,0,0,offsetof(OurBrushDeviceNode,LengthAccum),0,0,0,0,0,0,0,LA_UDF_SINGLE);
    
    LA_IDN_REGISTER("Brush Settings",L'🖌',OUR_IDN_BRUSH_SETTINGS,OUR_PC_IDN_BRUSH_SETTINGS, IDN_BrushSettingsInit, IDN_BrushSettingsDestroy, IDN_BrushSettingsVisit, IDN_BrushSettingsEval, OurBrushSettingsNode);
    LA_IDN_REGISTER("Brush Outputs",L'🖌',OUR_IDN_BRUSH_OUTPUTS,OUR_PC_IDN_BRUSH_OUTPUTS, IDN_BrushOutputsInit, IDN_BrushOutputsDestroy, IDN_BrushOutputsVisit, IDN_BrushOutputsEval, OurBrushOutputsNode);
    LA_IDN_REGISTER("Brush Device",L'🖳',OUR_IDN_BRUSH_DEVICE,OUR_PC_IDN_BRUSH_DEVICE, IDN_BrushDeviceInit, IDN_BrushDeviceDestroy, IDN_BrushDeviceVisit, IDN_BrushDeviceEval, OurBrushDeviceNode);
    
    laNodeCategory* nc=laAddNodeCategory("Our Paint",0,LA_RACK_TYPE_DRIVER);

    laNodeCategoryAddNodeTypes(LA_NODE_CATEGORY_DRIVER, &OUR_IDN_BRUSH_OUTPUTS,0);
    laNodeCategoryAddNodeTypes(nc, &OUR_IDN_BRUSH_DEVICE, 0);
    laNodeCategoryAddNodeTypes(nc, &OUR_IDN_BRUSH_SETTINGS, 0);
}


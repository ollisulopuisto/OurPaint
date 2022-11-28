#include "ourpaint.h"

OurPaint *Our;

const char OUR_CANVAS_SHADER[]="#version 430\n\
layout(local_size_x = 32, local_size_y = 32, local_size_z = 1) in;\n\
layout(rgba8, binding = 0) coherent uniform image2D img;\n\
layout(std430, binding = 1) buffer CCOLOR { float ccolor[4]; };\n\
uniform ivec2 Task;\n\
\n\
int dab(float d, vec4 color, float size, float hardness, float smudge, vec4 smudge_color, vec4 last_color, out vec4 final){\n\
    color.rgb=mix(color,smudge_color,smudge).rgb;\n\
    float a=clamp(color.a*1-pow(d/size,1+1/(1-hardness)),0,1);\n\
    final=vec4(mix(last_color.rgb,color.rgb,a), 1-(1-last_color.a)*(1-a));\n\
    return 1;\n\
}\n\
ivec2 DabPos(int i){\n\
    return ivec2(mod(i,500)+250,i/500*50+100);\n\
}\n\
void main() {\n\
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);\n\
    if(Task.x<1){\n\
        vec4 pixel =px.x>500?vec4(0.2,0.2,0.9,1.0):(px.x>400?vec4(0.0,0.0,0.0,1.0):(px.x>300?vec4(1.0,0.2,0.2,1.0):vec4(1.0,1.0,0.2,1.0)));\n\
        imageStore(img, px, pixel);\n\ return;}\n\
    int ii=Task.x;\n\
    float dd=distance(px,vec2(DabPos(ii*20))); if(dd>200) return;\n\
    vec4 final;\n\
    vec4 dabc=imageLoad(img, px);\n\
    memoryBarrier();barrier(); \n\
    vec4 smugec=imageLoad(img, DabPos(ii*20));\n\
    int any=0;\n\
    for(int t=0;t<20;t++){\n\
        int i=t+ii*20;\n\
        float size=15;\n\
        float d=distance(px,vec2(DabPos(i))); if(d>size) continue;\n\
        dab(d,vec4(1,1,1,0.1),size,0.95,0.95,smugec,dabc,final);\n\
        dabc=final;\n\
    }\n\
    if(dd!=0) imageStore(img, px, dabc); //else ccolor=dabc;\n\
}\n\
";

OurLayer* our_NewLayer(char* name){
    OurLayer* l=memAcquire(sizeof(OurLayer)); strSafeSet(&l->Name,name); lstPushItem(&Our->Layers, l);
    return l;
}
void our_RemoveLayer(OurLayer* l){
    strSafeDestroy(&l->Name); lstRemoveItem(&Our->Layers, l);
}

void ourinv_NewLayer(laOperator* a, laEvent* e){
    our_NewLayer("New Layer"); laNotifyUsers("our.canvas.layers");
    return LA_FINISHED;
}
void ourinv_RemoveLayer(laOperator* a, laEvent* e){
    OurLayer* l=a->This?a->This->EndInstance:0; if(!l) return LA_CANCELED;
    our_RemoveLayer(l); laNotifyUsers("our.canvas.layers");
    return LA_FINISHED;
}


void ourInit(){
    Our=memAcquire(sizeof(OurPaint));

    laCreateOperatorType("OUR_new_layer","New Layer","Create a new layer",0,0,0,ourinv_NewLayer,0,'+',0);
    laCreateOperatorType("OUR_remove_layer","Remove Layer","Remove this layer",0,0,0,ourinv_RemoveLayer,0,L'🗴',0);

    char error[1024]; int status;

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

    Our->CanvasTaskUniform=glGetUniformLocation(Our->CanvasProgram,"Task");

    tnsEnableShaderv(0);
}


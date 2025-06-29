import os,subprocess,re

template="""version: 1
AppDir:
  path: /home/yiming/Documents/sync/Projects/2022/laprograms/app/AppDir
  app_info:
    id: chengdu.littlea.ourpaint
    name: OurPaint
    icon: application-x-executable
    version: v0.5
    exec: OurPaint
    exec_args: $@
  apt:
    arch:
    - amd64
    allow_unauthenticated: true
    sources:
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy main restricted
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy-updates main restricted
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy universe
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy-updates universe
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy multiverse
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy-updates multiverse
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy-backports main restricted universe multiverse
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy-security main restricted
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy-security universe
    - sourceline: deb http://mirrors.aliyun.com/ubuntu/ jammy-security multiverse
    include:
    - libc6:amd64
    - locales
---includes---
  files:
    include:
    - lib64/ld-linux-x86-64.so.2
---libs---
    exclude:
    - usr/share/man
    - usr/share/doc/*/README.*
    - usr/share/doc/*/changelog.*
    - usr/share/doc/*/NEWS.*
    - usr/share/doc/*/TODO.*
AppImage:
  arch: x86_64
  update-information: guess
"""

script="""appimage-builder --recipe AppImageBuilder.yml
mv OurPaint*.AppImage OurPaint/
tar -cvzf OurPaint.tar.gz OurPaint/
"""

os.system("rm -rf ../OurPaintApp/AppDir")
os.system("rm -rf ../OurPaintApp/OurPaint")
os.system("mkdir -p ../OurPaintApp/AppDir")
os.system("mkdir -p ../OurPaintApp/OurPaint/fonts")
os.system("mkdir -p ../OurPaintApp/OurPaint/profiles")
os.system("cp build/OurPaint ../OurPaintApp/AppDir")
os.system("cp README.md ../OurPaintApp/OurPaint")
os.system("cp default_brushes.udf ../OurPaintApp/OurPaint")
os.system("cp default_pallettes.udf ../OurPaintApp/OurPaint")
os.system("cp default_canvases.udf ../OurPaintApp/OurPaint")
os.system("cp default_pigments.udf ../OurPaintApp/OurPaint")
os.system("cp default_lights.udf ../OurPaintApp/OurPaint")
os.system("cp COPYING ../OurPaintApp/OurPaint")
os.system("cp COPYING_CC_BY_NC ../OurPaintApp/OurPaint")
os.system("cp %s/.local/share/fonts/lagui/*.* ../OurPaintApp/OurPaint/fonts"%os.path.expanduser("~"))
os.system("cp profiles/*.* ../OurPaintApp/OurPaint/profiles")

additional=""
# bundle everything?
#result = subprocess.check_output("ldd build/OurPaint", shell=True).decode("utf-8")
#for ma in re.finditer(r".*\=\>\s(\S+)",result):
#    additional=additional+"    - "+ma.group(1)+"\n"

template=template.replace("---libs---",additional)

additional="""
    - libfreetype6
    - liblcms2-2
    - libpng16-16
    - libxi6
    - libxfixes3
    - libxcursor1
    - libxrandr2
    - libglew2.2
"""
template=template.replace("---includes---",additional)

f=open("../OurPaintApp/AppImageBuilder.yml","w")
f.write(template)
f.close()
f=open("../OurPaintApp/run.sh","w")
f.write(script)
f.close()

#os.system("cd ../OurPaintApp/ && sh run.sh")
#os.system("")

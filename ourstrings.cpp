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

const char OUR_MIME[]=R"(
<?xml version="1.0" encoding="utf-8"?>
<mime-type xmlns="http://www.freedesktop.org/standards/shared-mime-info" type="image/ourpaint">
  <comment>OurPaint project file</comment>
  <icon name="image"/>
  <glob pattern="*.ourpaint"/>
</mime-type>    
)";

const char OUR_THUMBNAILER[]=R"(
[Thumbnailer Entry]
Version=0.3
Encoding=UTF-8
Type=X-Thumbnailer
Name=OurPaint thumbnailer
MimeType=image/ourpaint;
Exec=%OURPAINT_EXEC% --thumbnail %i %o
)";

const char OUR_DESKTOP[]=R"(
[Desktop Entry]
Encoding=UTF-8
Version=0.3
Type=Application
Terminal=false
Path=%OURPAINT_DIR%
Exec=%OURPAINT_EXEC% %f
Name=Our Paint
Icon=%OURPAINT_ICON%
Categories=Graphics;
MimeType=image/ourpaint;
PrefersNonDefaultGPU=true
)";

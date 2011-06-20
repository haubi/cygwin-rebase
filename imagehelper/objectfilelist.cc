/*
 * Copyright (c) 2002 Ralf Habacker  <Ralf.Habacker@freenet.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * $Id$    
 */

#include <stdlib.h>
#include <iostream>

#include "objectfilelist.h"


// read a dll into the cache

#ifdef __CYGWIN__
#include <sys/cygwin.h>
#endif

ObjectFileList::ObjectFileList()
{
  count = 0;
}

bool ObjectFileList::add
  (ObjectFile *obj)
  {
    if (count < 1000)
      {
        list[count++] = obj;
        return true;
      }
    return false;
  }


ObjectFile *ObjectFileList::getNext(void)
{
  if (iterator < count)
    return list[iterator++];
  else
    return 0;
}

ObjectFile *ObjectFileList::get
  (char *FileName)
  {
    ObjectFile *p;
    reset();
    while (p = getNext())
      {
        if (strstr(FileName,p->getFileName()))
          return p;
      }
    return 0;
  }

ObjectFileList::~ObjectFileList()
{
  ObjectFile *p;
  reset();
  while (p = getNext())
    delete p;
}

#ifdef OBJECTFILELIST_MAIN
int main(int argc, char **argv)
{
  ObjectFileList test;
  test.Add("/bin/cygz.dll");
  ObjectFile *dll = test.getObjectFile("/bin/cygz.dll");
  if (dll)
    std::cout << dll->getFileName() << std::endl;
  return 0;
}

#endif

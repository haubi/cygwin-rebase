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

#ifndef OBJECTFILELIST_H
#define OBJECTFILELIST_H

#include "objectfile.h"

class ObjectFileList
  {

  public:
    ObjectFileList();

    // add objectfile to the list
    bool add
      (ObjectFile *obj);

    ObjectFile *get
    (char *FileName);

    // reset iterator
    void reset(void)
    {
      iterator = 0;
    }

    // get next list element
    ObjectFile *getNext(void);

    // get number of elements
    int getCount(void)
    {
      return count;
    }

    // destructor
    ~ObjectFileList();
  private:
    int count;
    int iterator;
    ObjectFile *list[1000];

  };

#endif

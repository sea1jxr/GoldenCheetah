/*
 * Copyright (c) 2013 Mark Liversedge (liversedge@gmail.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#ifndef _GC_ComparePane_h
#define _GC_ComparePane_h 1

#include <QWidget>
#include "GcSideBarItem.h"

class ComparePane : public QWidget
{
    Q_OBJECT;

    public:
        enum mode { season, interval };
        typedef enum mode CompareMode;

        ComparePane(QWidget *parent, CompareMode mode=interval);

    protected:
        void dragEnterEvent(QDragEnterEvent*);
        void dragLeaveEvent(QDragLeaveEvent*);
        void dropEvent(QDropEvent *);

    signals:

    private slots:

    protected:

    private:
    CompareMode mode_; // remember the mode we were created as...
};

#endif // _GC_ComparePane_h

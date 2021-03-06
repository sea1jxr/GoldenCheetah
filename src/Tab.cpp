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


#include "Tab.h"
#include "Views.h"
#include "MainWindow.h"

Tab::Tab(Context *context) : QWidget(context->mainWindow), context(context)
{
    context->tab = this;

    setContentsMargins(0,0,0,0);
    QVBoxLayout *main = new QVBoxLayout(this);
    main->setSpacing(0);
    main->setContentsMargins(0,0,0,0);

    views = new QStackedWidget(this);
    views->setContentsMargins(0,0,0,0);
    main->addWidget(views);

    // all the stack views for the controls
    masterControls = new QStackedWidget(this);
    masterControls->setFrameStyle(QFrame::Plain | QFrame::NoFrame);
    masterControls->setCurrentIndex(0);
    masterControls->setContentsMargins(0,0,0,0);

    // Home
    homeControls = new QStackedWidget(this);
    homeControls->setFrameStyle(QFrame::Plain | QFrame::NoFrame);
    homeControls->setContentsMargins(0,0,0,0);
    masterControls->addWidget(homeControls);
    homeView = new HomeView(context, homeControls);
    views->addWidget(homeView);

    // Analysis
    analysisControls = new QStackedWidget(this);
    analysisControls->setFrameStyle(QFrame::Plain | QFrame::NoFrame);
    analysisControls->setCurrentIndex(0);
    analysisControls->setContentsMargins(0,0,0,0);
    masterControls->addWidget(analysisControls);
    analysisView = new AnalysisView(context, analysisControls);
    views->addWidget(analysisView);

    // Diary
    diaryControls = new QStackedWidget(this);
    diaryControls->setFrameStyle(QFrame::Plain | QFrame::NoFrame);
    diaryControls->setCurrentIndex(0);
    diaryControls->setContentsMargins(0,0,0,0);
    masterControls->addWidget(diaryControls);
    diaryView = new DiaryView(context, diaryControls);
    views->addWidget(diaryView);

    // Train
    trainControls = new QStackedWidget(this);
    trainControls->setFrameStyle(QFrame::Plain | QFrame::NoFrame);
    trainControls->setCurrentIndex(0);
    trainControls->setContentsMargins(0,0,0,0);
    masterControls->addWidget(trainControls);
    trainView = new TrainView(context, trainControls);
    views->addWidget(trainView);

    // the dialog box for the chart settings
    chartSettings = new ChartSettings(this, masterControls);
    chartSettings->setMaximumWidth(450);
    chartSettings->setMaximumHeight(600);
    chartSettings->hide();
}

Tab::~Tab()
{
    delete analysisView;
    delete homeView;
    delete trainView;
    delete diaryView;
    delete views;
}

RideNavigator *Tab::rideNavigator()
{
    analysisView->rideNavigator();
}

void
Tab::close()
{
    analysisView->saveState();
    homeView->saveState();
    trainView->saveState();
    diaryView->saveState();

    analysisView->close();
    homeView->close();
    trainView->close();
    diaryView->close();
}

/******************************************************************************
 * MainWindow integration with Tab / TabView (mostly pass through)
 *****************************************************************************/

void Tab::setShowBottom(bool x) { view(currentView())->setShowBottom(x); }
bool Tab::isShowBottom() { return view(currentView())->isShowBottom(); }
bool Tab::hasBottom() { return view(currentView())->hasBottom(); }
void Tab::setSidebarEnabled(bool x) { view(currentView())->setSidebarEnabled(x); }
bool Tab::isSidebarEnabled() { return view(currentView())->sidebarEnabled(); }
void Tab::toggleSidebar() { view(currentView())->setSidebarEnabled(!view(currentView())->sidebarEnabled()); }
void Tab::setTiled(bool x) { view(currentView())->setTiled(x); }
bool Tab::isTiled() { return view(currentView())->isTiled(); }
void Tab::toggleTile() { view(currentView())->setTiled(!view(currentView())->isTiled()); }
void Tab::resetLayout() { view(currentView())->resetLayout(); }
void Tab::addChart(GcWinID i) { view(currentView())->addChart(i); }
void Tab::addIntervals() { analysisView->addIntervals(); }

void Tab::setRide(RideItem*ride) 
{ 
    analysisView->setRide(ride);
    homeView->setRide(ride);
    trainView->setRide(ride);
    diaryView->setRide(ride);
}

TabView *
Tab::view(int index)
{
    switch(index) {
        case 0 : return homeView;
        default:
        case 1 : return analysisView;
        case 2 : return diaryView;
        case 3 : return trainView;
    }
}

void
Tab::selectView(int index)
{
    // first we deselect the current
    view(views->currentIndex())->setSelected(false);

    // now select the real one
    views->setCurrentIndex(index);
    view(index)->setSelected(true);
    masterControls->setCurrentIndex(index);
}

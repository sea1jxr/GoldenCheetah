/*
 * Copyright (c) 2006 Sean C. Rhea (srhea@srhea.net)
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

#include "AllPlot.h"
#include "Context.h"
#include "Athlete.h"
#include "AllPlotWindow.h"
#include "ReferenceLineDialog.h"
#include "RideFile.h"
#include "RideItem.h"
#include "IntervalItem.h"
#include "Settings.h"
#include "Units.h"
#include "Zones.h"
#include "Colors.h"
#include "WPrime.h"

#include <qwt_plot_curve.h>
#include <qwt_plot_intervalcurve.h>
#include <qwt_plot_grid.h>
#include <qwt_plot_layout.h>
#include <qwt_plot_marker.h>
#include <qwt_scale_div.h>
#include <qwt_scale_widget.h>
#include <qwt_compat.h>
#include <qwt_text.h>
#include <qwt_legend.h>
#include <qwt_series_data.h>
#include <QMultiMap>

class IntervalPlotData : public QwtSeriesData<QPointF>
{
    public:
    IntervalPlotData(AllPlot *allPlot, Context *context) :
        allPlot(allPlot), context(context) {}
    double x(size_t i) const ;
    double y(size_t i) const ;
    size_t size() const ;
    //virtual QwtData *copy() const ;
    void init() ;
    IntervalItem *intervalNum(int n) const;
    int intervalCount() const;
    AllPlot *allPlot;
    Context *context;

    virtual QPointF sample(size_t i) const;
    virtual QRectF boundingRect() const;
};

// define a background class to handle shading of power zones
// draws power zone bands IF zones are defined and the option
// to draw bonds has been selected
class AllPlotBackground: public QwtPlotItem
{
    private:
        AllPlot *parent;

    public:
        AllPlotBackground(AllPlot *_parent)
        {
            setZ(0.0);
            parent = _parent;
        }

    virtual int rtti() const
    {
        return QwtPlotItem::Rtti_PlotUserItem;
    }

    virtual void draw(QPainter *painter,
                      const QwtScaleMap &, const QwtScaleMap &yMap,
                      const QRectF &rect) const
    {
        RideItem *rideItem = parent->rideItem;

        if (! rideItem)
            return;

        const Zones *zones       = rideItem->zones;
        int zone_range     = rideItem->zoneRange();
        if (parent->shadeZones() && (zone_range >= 0)) {
            QList <int> zone_lows = zones->getZoneLows(zone_range);
            int num_zones = zone_lows.size();
            if (num_zones > 0) {
                for (int z = 0; z < num_zones; z ++) {
                    QRect r = rect.toRect();

                    QColor shading_color = zoneColor(z, num_zones);
                    shading_color.setHsv(
                        shading_color.hue(),
                        shading_color.saturation() / 4,
                        shading_color.value()
                        );
                    r.setBottom(yMap.transform(zone_lows[z]));
                    if (z + 1 < num_zones)
                        r.setTop(yMap.transform(zone_lows[z + 1]));
                    if (r.top() <= r.bottom())
                        painter->fillRect(r, shading_color);
                }
            }
        } else {
        }
    }
};

// Zone labels are drawn if power zone bands are enabled, automatically
// at the center of the plot
class AllPlotZoneLabel: public QwtPlotItem
{
    private:
        AllPlot *parent;
        int zone_number;
        double watts;
        QwtText text;

    public:
        AllPlotZoneLabel(AllPlot *_parent, int _zone_number)
        {
            parent = _parent;
            zone_number = _zone_number;

            RideItem *rideItem = parent->rideItem;


            if (! rideItem)
                return;


            const Zones *zones       = rideItem->zones;
            int zone_range     = rideItem->zoneRange();

            // create new zone labels if we're shading
            if (parent->shadeZones() && (zone_range >= 0)) {
                QList <int> zone_lows = zones->getZoneLows(zone_range);
                QList <QString> zone_names = zones->getZoneNames(zone_range);
                int num_zones = zone_lows.size();
                if (zone_names.size() != num_zones) return;
                if (zone_number < num_zones) {
                    watts =
                        (
                            (zone_number + 1 < num_zones) ?
                            0.5 * (zone_lows[zone_number] + zone_lows[zone_number + 1]) :
                            (
                                (zone_number > 0) ?
                                (1.5 * zone_lows[zone_number] - 0.5 * zone_lows[zone_number - 1]) :
                                2.0 * zone_lows[zone_number]
                            )
                        );

                    text = QwtText(zone_names[zone_number]);
                    if (_parent->referencePlot == NULL) {
                        text.setFont(QFont("Helvetica",24, QFont::Bold));
                    } else {
                        text.setFont(QFont("Helvetica",12, QFont::Bold));
                    }

                    QColor text_color = zoneColor(zone_number, num_zones);
                    text_color.setAlpha(64);
                    text.setColor(text_color);
                }
            }

            setZ(1.0 + zone_number / 100.0);
        }
        virtual int rtti() const
        {
            return QwtPlotItem::Rtti_PlotUserItem;
        }

        void draw(QPainter *painter,
                  const QwtScaleMap &, const QwtScaleMap &yMap,
                  const QRectF &rect) const
        {
            if (parent->shadeZones()) {
                int x = (rect.left() + rect.right()) / 2;
                int y = yMap.transform(watts);

                // the following code based on source for QwtPlotMarker::draw()
                QRect tr(QPoint(0, 0), text.textSize(painter->font()).toSize());
                tr.moveCenter(QPoint(x, y));
                text.draw(painter, tr);
            }
        }
};

class TimeScaleDraw: public QwtScaleDraw
{

    public:

    TimeScaleDraw(bool *bydist) : QwtScaleDraw(), bydist(bydist) {}

    virtual QwtText label(double v) const
    {
        if (*bydist) {
            return QString("%1").arg(v);
        } else {
            QTime t = QTime().addSecs(v*60.00);
            if (scaleMap().sDist() > 5)
                return t.toString("hh:mm");
            return t.toString("hh:mm:ss");
        }
    }
    private:
    bool *bydist;

};


static inline double
max(double a, double b) { if (a > b) return a; else return b; }

AllPlot::AllPlot(AllPlotWindow *parent, Context *context):
    QwtPlot(parent),
    rideItem(NULL),
    shade_zones(true),
    showPowerState(3),
    showNP(false),
    showXP(false),
    showAP(false),
    showHr(true),
    showSpeed(true),
    showCad(true),
    showAlt(true),
    showTemp(true),
    showWind(true),
    showTorque(true),
    showBalance(true),
    bydist(false),
    context(context),
    parent(parent)
{
    setInstanceName("AllPlot");

    referencePlot = NULL;

    if (appsettings->value(this, GC_SHADEZONES, true).toBool()==false)
        shade_zones = false;

    smooth = 1;

    // create a background object for shading
    bg = new AllPlotBackground(this);
    bg->attach(this);

    //insertLegend(new QwtLegend(), QwtPlot::BottomLegend);
    setCanvasBackground(GColor(CRIDEPLOTBACKGROUND));
    canvas()->setFrameStyle(QFrame::NoFrame);

    setXTitle();

    wattsCurve = new QwtPlotCurve(tr("Power"));
    wattsCurve->setYAxis(yLeft);

    npCurve = new QwtPlotCurve(tr("NP"));
    npCurve->setYAxis(yLeft);

    xpCurve = new QwtPlotCurve(tr("xPower"));
    xpCurve->setYAxis(yLeft);

    apCurve = new QwtPlotCurve(tr("aPower"));
    apCurve->setYAxis(yLeft);

    hrCurve = new QwtPlotCurve(tr("Heart Rate"));
    hrCurve->setYAxis(yLeft2);

    speedCurve = new QwtPlotCurve(tr("Speed"));
    speedCurve->setYAxis(yRight);

    cadCurve = new QwtPlotCurve(tr("Cadence"));
    cadCurve->setYAxis(yLeft2);

    altCurve = new QwtPlotCurve(tr("Altitude"));
    // altCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    altCurve->setYAxis(yRight2);

    tempCurve = new QwtPlotCurve(tr("Temperature"));
    if (context->athlete->useMetricUnits)
        tempCurve->setYAxis(yRight); // with speed
    else
        tempCurve->setYAxis(yLeft2); // with cadence

    windCurve = new QwtPlotIntervalCurve(tr("Wind"));
    windCurve->setYAxis(yRight);

    torqueCurve = new QwtPlotCurve(tr("Torque"));
    torqueCurve->setYAxis(yRight);

    balanceLCurve = new QwtPlotCurve(tr("Left Balance"));
    balanceLCurve->setYAxis(yLeft2);

    balanceRCurve = new QwtPlotCurve(tr("Right Balance"));
    balanceRCurve->setYAxis(yLeft2);

    wCurve = new QwtPlotCurve(tr("W' Balance (j)"));
    wCurve->setYAxis(yRight3);

    mCurve = new QwtPlotCurve(tr("Matches"));
    mCurve->setStyle(QwtPlotCurve::Dots);
    mCurve->setYAxis(yRight3);

    curveTitle.attach(this);
    curveTitle.setLabelAlignment(Qt::AlignRight);

    intervalHighlighterCurve = new QwtPlotCurve();
    intervalHighlighterCurve->setYAxis(yLeft);
    intervalHighlighterCurve->setData(new IntervalPlotData(this, context));
    intervalHighlighterCurve->attach(this);
    //this->legend()->remove(intervalHighlighterCurve); // don't show in legend

    // setup that grid
    grid = new QwtPlotGrid();
    grid->enableX(true);
    grid->enableY(true);
    grid->attach(this);

    // get rid of nasty blank space on right of the plot
    plotLayout()->setAlignCanvasToScales(true);
    setAxisMaxMinor(xBottom, 0);
    setAxisMaxMinor(yLeft, 0);
    setAxisMaxMinor(yLeft2, 0);
    setAxisMaxMinor(yRight, 0);
    setAxisMaxMinor(yRight2, 0);

    axisWidget(QwtPlot::yLeft)->installEventFilter(this);

    configChanged(); // set colors
}

void
AllPlot::configChanged()
{
    double width = appsettings->value(this, GC_LINEWIDTH, 2.0).toDouble();

    if (appsettings->value(this, GC_ANTIALIAS, false).toBool() == true) {
        wattsCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        npCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        xpCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        apCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        wCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        mCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        hrCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        speedCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        cadCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        altCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        tempCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        windCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        torqueCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        balanceLCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        balanceRCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
        intervalHighlighterCurve->setRenderHint(QwtPlotItem::RenderAntialiased);
    }

    setCanvasBackground(GColor(CRIDEPLOTBACKGROUND));
    QPen wattsPen = QPen(GColor(CPOWER));
    wattsPen.setWidth(width);
    wattsCurve->setPen(wattsPen);
    QPen npPen = QPen(GColor(CNPOWER));
    npPen.setWidth(width);
    npCurve->setPen(npPen);
    QPen xpPen = QPen(GColor(CXPOWER));
    xpPen.setWidth(width);
    xpCurve->setPen(xpPen);
    QPen apPen = QPen(GColor(CAPOWER));
    apPen.setWidth(width);
    apCurve->setPen(apPen);
    QPen hrPen = QPen(GColor(CHEARTRATE));
    hrPen.setWidth(width);
    hrCurve->setPen(hrPen);
    QPen speedPen = QPen(GColor(CSPEED));
    speedPen.setWidth(width);
    speedCurve->setPen(speedPen);
    QPen cadPen = QPen(GColor(CCADENCE));
    cadPen.setWidth(width);
    cadCurve->setPen(cadPen);
    QPen altPen(GColor(CALTITUDE));
    altPen.setWidth(width);
    altCurve->setPen(altPen);
    QColor brush_color = GColor(CALTITUDEBRUSH);
    brush_color.setAlpha(200);
    altCurve->setBrush(brush_color);   // fill below the line
    QPen tempPen = QPen(GColor(CTEMP));
    tempPen.setWidth(width);
    tempCurve->setPen(tempPen);
    if (smooth == 1)
        tempCurve->setStyle(QwtPlotCurve::Dots);
    else
        tempCurve->setStyle(QwtPlotCurve::Lines);
    //QPen windPen = QPen(GColor(CWINDSPEED));
    //windPen.setWidth(width);
    windCurve->setPen(Qt::NoPen);
    QColor wbrush_color = GColor(CWINDSPEED);
    wbrush_color.setAlpha(200);
    windCurve->setBrush(wbrush_color);   // fill below the line
    QPen torquePen = QPen(GColor(CTORQUE));
    torquePen.setWidth(width);
    torqueCurve->setPen(torquePen);
    QPen balanceLPen = QPen(GColor(CBALANCERIGHT));
    balanceLPen.setWidth(width);
    balanceLCurve->setPen(balanceLPen);
    QColor brbrush_color = GColor(CBALANCERIGHT);
    brbrush_color.setAlpha(200);
    balanceLCurve->setBrush(brbrush_color);   // fill below the line
    QPen balanceRPen = QPen(GColor(CBALANCELEFT));
    balanceRPen.setWidth(width);
    balanceRCurve->setPen(balanceRPen);
    QColor blbrush_color = GColor(CBALANCELEFT);
    blbrush_color.setAlpha(200);
    balanceRCurve->setBrush(blbrush_color);   // fill below the line
    QPen wPen = QPen(GColor(CWBAL)); 
    wPen.setWidth(2); // thicken
    wCurve->setPen(wPen);
    QwtSymbol sym;
    sym.setStyle(QwtSymbol::Rect);
    sym.setPen(QPen(QColor(255,127,0))); // orange like a match, will make configurable later
    sym.setSize(4);
    mCurve->setSymbol(new QwtSymbol(sym));
    QPen ihlPen = QPen(GColor(CINTERVALHIGHLIGHTER));
    ihlPen.setWidth(width);
    intervalHighlighterCurve->setPen(ihlPen);
    QColor ihlbrush = QColor(GColor(CINTERVALHIGHLIGHTER));
    ihlbrush.setAlpha(128);
    intervalHighlighterCurve->setBrush(ihlbrush);   // fill below the line
    //this->legend()->remove(intervalHighlighterCurve); // don't show in legend
    QPen gridPen(GColor(CPLOTGRID));
    gridPen.setStyle(Qt::DotLine);
    grid->setPen(gridPen);

    // curve brushes
    if (parent->isPaintBrush()) {
        QColor p;
        p = wattsCurve->pen().color();
        p.setAlpha(64);
        wattsCurve->setBrush(QBrush(p));

        p = npCurve->pen().color();
        p.setAlpha(64);
        npCurve->setBrush(QBrush(p));

        p = xpCurve->pen().color();
        p.setAlpha(64);
        xpCurve->setBrush(QBrush(p));

        p = apCurve->pen().color();
        p.setAlpha(64);
        apCurve->setBrush(QBrush(p));

        p = wCurve->pen().color();
        p.setAlpha(64);
        wCurve->setBrush(QBrush(p));

        p = hrCurve->pen().color();
        p.setAlpha(64);
        hrCurve->setBrush(QBrush(p));

        p = speedCurve->pen().color();
        p.setAlpha(64);
        speedCurve->setBrush(QBrush(p));

        p = cadCurve->pen().color();
        p.setAlpha(64);
        cadCurve->setBrush(QBrush(p));

        p = torqueCurve->pen().color();
        p.setAlpha(64);
        torqueCurve->setBrush(QBrush(p));

        /*p = balanceLCurve->pen().color();
        p.setAlpha(64);
        balanceLCurve->setBrush(QBrush(p));

        p = balanceRCurve->pen().color();
        p.setAlpha(64);
        balanceRCurve->setBrush(QBrush(p));*/
    } else {
        wattsCurve->setBrush(Qt::NoBrush);
        npCurve->setBrush(Qt::NoBrush);
        xpCurve->setBrush(Qt::NoBrush);
        apCurve->setBrush(Qt::NoBrush);
        wCurve->setBrush(Qt::NoBrush);
        hrCurve->setBrush(Qt::NoBrush);
        speedCurve->setBrush(Qt::NoBrush);
        cadCurve->setBrush(Qt::NoBrush);
        torqueCurve->setBrush(Qt::NoBrush);
        //balanceLCurve->setBrush(Qt::NoBrush);
        //balanceRCurve->setBrush(Qt::NoBrush);
    }

    QPalette pal;

    // tick draw
    TimeScaleDraw *tsd = new TimeScaleDraw(&this->bydist) ;
    tsd->setTickLength(QwtScaleDiv::MajorTick, 3);
    setAxisScaleDraw(QwtPlot::xBottom, tsd);
    pal.setColor(QPalette::WindowText, GColor(CPLOTMARKER));
    pal.setColor(QPalette::Text, GColor(CPLOTMARKER));
    axisWidget(QwtPlot::xBottom)->setPalette(pal);

    QwtScaleDraw *sd = new QwtScaleDraw;
    sd->setTickLength(QwtScaleDiv::MajorTick, 3);
    sd->enableComponent(QwtScaleDraw::Ticks, false);
    sd->enableComponent(QwtScaleDraw::Backbone, false);
    setAxisScaleDraw(QwtPlot::yLeft, sd);
    pal.setColor(QPalette::WindowText, GColor(CPOWER));
    pal.setColor(QPalette::Text, GColor(CPOWER));
    axisWidget(QwtPlot::yLeft)->setPalette(pal);

    sd = new QwtScaleDraw;
    sd->setTickLength(QwtScaleDiv::MajorTick, 3);
    sd->enableComponent(QwtScaleDraw::Ticks, false);
    sd->enableComponent(QwtScaleDraw::Backbone, false);
    setAxisScaleDraw(QwtPlot::yLeft2, sd);
    pal.setColor(QPalette::WindowText, GColor(CHEARTRATE));
    pal.setColor(QPalette::Text, GColor(CHEARTRATE));
    axisWidget(QwtPlot::yLeft2)->setPalette(pal);

    sd = new QwtScaleDraw;
    sd->setTickLength(QwtScaleDiv::MajorTick, 3);
    sd->enableComponent(QwtScaleDraw::Ticks, false);
    sd->enableComponent(QwtScaleDraw::Backbone, false);
    setAxisScaleDraw(QwtPlot::yRight, sd);
    pal.setColor(QPalette::WindowText, GColor(CSPEED));
    pal.setColor(QPalette::Text, GColor(CSPEED));
    axisWidget(QwtPlot::yRight)->setPalette(pal);

    sd = new QwtScaleDraw;
    sd->setTickLength(QwtScaleDiv::MajorTick, 3);
    sd->enableComponent(QwtScaleDraw::Ticks, false);
    sd->enableComponent(QwtScaleDraw::Backbone, false);
    setAxisScaleDraw(QwtPlot::yRight2, sd);
    pal.setColor(QPalette::WindowText, GColor(CALTITUDE));
    pal.setColor(QPalette::Text, GColor(CALTITUDE));
    axisWidget(QwtPlot::yRight2)->setPalette(pal);

    sd = new QwtScaleDraw;
    sd->enableComponent(QwtScaleDraw::Ticks, false);
    sd->enableComponent(QwtScaleDraw::Backbone, false);
    sd->setLabelRotation(90);// in the 000s
    sd->setTickLength(QwtScaleDiv::MajorTick, 3);
    setAxisScaleDraw(QwtPlot::yRight3, sd);
    pal.setColor(QPalette::WindowText, GColor(CWBAL));
    pal.setColor(QPalette::Text, GColor(CWBAL));
    axisWidget(QwtPlot::yRight3)->setPalette(pal);
}

struct DataPoint {
    double time, hr, watts, np, ap, xp, speed, cad, alt, temp, wind, torque, lrbalance;
    DataPoint(double t, double h, double w, double n, double l, double x, double s, double c, double a, double te, double wi, double tq, double lrb) :
        time(t), hr(h), watts(w), np(n), ap(l), xp(x), speed(s), cad(c), alt(a), temp(te), wind(wi), torque(tq), lrbalance(lrb) {}
};

bool AllPlot::shadeZones() const
{
    return shade_zones;
}

void
AllPlot::setAxisTitle(int axis, QString label)
{
    // setup the default fonts
    QFont stGiles; // hoho - Chart Font St. Giles ... ok you have to be British to get this joke
    stGiles.fromString(appsettings->value(this, GC_FONT_CHARTLABELS, QFont().toString()).toString());
    stGiles.setPointSize(appsettings->value(NULL, GC_FONT_CHARTLABELS_SIZE, 8).toInt());

    QwtText title(label);
    title.setFont(stGiles);
    QwtPlot::setAxisFont(axis, stGiles);
    QwtPlot::setAxisTitle(axis, title);
}

void AllPlot::refreshZoneLabels()
{
    foreach(AllPlotZoneLabel *label, zoneLabels) {
        label->detach();
        delete label;
    }
    zoneLabels.clear();

    if (rideItem) {
        int zone_range = rideItem->zoneRange();
        const Zones *zones = rideItem->zones;

        // generate labels for existing zones
        if (zone_range >= 0) {
            int num_zones = zones->numZones(zone_range);
            for (int z = 0; z < num_zones; z ++) {
                AllPlotZoneLabel *label = new AllPlotZoneLabel(this, z);
                label->attach(this);
                zoneLabels.append(label);
            }
        }
    }
}


void
AllPlot::recalc()
{
    if (referencePlot !=NULL){
        return;
    }

    if (timeArray.empty())
        return;

    int rideTimeSecs = (int) ceil(timeArray[arrayLength - 1]);
    if (rideTimeSecs > 7*24*60*60) {
        QwtArray<double> data;
        QVector<QwtIntervalSample> intData;

        wCurve->setData(data,data);
        mCurve->setData(data,data);
        if (!npArray.empty())
            npCurve->setData(data, data);
        if (!xpArray.empty())
            xpCurve->setData(data, data);
        if (!apArray.empty())
            apCurve->setData(data, data);
        if (!wattsArray.empty())
            wattsCurve->setData(data, data);
        if (!hrArray.empty())
            hrCurve->setData(data, data);
        if (!speedArray.empty())
            speedCurve->setData(data, data);
        if (!cadArray.empty())
            cadCurve->setData(data, data);
        if (!altArray.empty())
            altCurve->setData(data, data);
        if (!tempArray.empty())
            tempCurve->setData(data, data);
        if (!windArray.empty())
            windCurve->setData(new QwtIntervalSeriesData(intData));
        if (!torqueArray.empty())
            torqueCurve->setData(data, data);
        if (!balanceArray.empty())
            balanceLCurve->setData(data, data);
        if (!balanceArray.empty())
            balanceRCurve->setData(data, data);

        return;
    }
    // we should only smooth the curves if smoothed rate is greater than sample rate
    if (smooth > rideItem->ride()->recIntSecs()) {

        double totalWatts = 0.0;
        double totalNP = 0.0;
        double totalXP = 0.0;
        double totalAP = 0.0;
        double totalHr = 0.0;
        double totalSpeed = 0.0;
        double totalCad = 0.0;
        double totalDist = 0.0;
        double totalAlt = 0.0;
        double totalTemp = 0.0;
        double totalWind = 0.0;
        double totalTorque = 0.0;
        double totalBalance = 0.0;

        QList<DataPoint> list;

        smoothWatts.resize(rideTimeSecs + 1); //(rideTimeSecs + 1);
        smoothNP.resize(rideTimeSecs + 1); //(rideTimeSecs + 1);
        smoothXP.resize(rideTimeSecs + 1); //(rideTimeSecs + 1);
        smoothAP.resize(rideTimeSecs + 1); //(rideTimeSecs + 1);
        smoothHr.resize(rideTimeSecs + 1);
        smoothSpeed.resize(rideTimeSecs + 1);
        smoothCad.resize(rideTimeSecs + 1);
        smoothTime.resize(rideTimeSecs + 1);
        smoothDistance.resize(rideTimeSecs + 1);
        smoothAltitude.resize(rideTimeSecs + 1);
        smoothTemp.resize(rideTimeSecs + 1);
        smoothWind.resize(rideTimeSecs + 1);
        smoothRelSpeed.resize(rideTimeSecs + 1);
        smoothTorque.resize(rideTimeSecs + 1);
        smoothBalanceL.resize(rideTimeSecs + 1);
        smoothBalanceR.resize(rideTimeSecs + 1);

        for (int secs = 0; ((secs < smooth)
                            && (secs < rideTimeSecs)); ++secs) {
            smoothWatts[secs] = 0.0;
            smoothNP[secs] = 0.0;
            smoothXP[secs] = 0.0;
            smoothAP[secs] = 0.0;
            smoothHr[secs]    = 0.0;
            smoothSpeed[secs] = 0.0;
            smoothCad[secs]   = 0.0;
            smoothTime[secs]  = secs / 60.0;
            smoothDistance[secs]  = 0.0;
            smoothAltitude[secs]  = 0.0;
            smoothTemp[secs]  = 0.0;
            smoothWind[secs]  = 0.0;
            smoothRelSpeed[secs] = QwtIntervalSample();
            smoothTorque[secs]  = 0.0;
            smoothBalanceL[secs]  = 50;
            smoothBalanceR[secs]  = 50;
        }

        int i = 0;
        for (int secs = smooth; secs <= rideTimeSecs; ++secs) {
            while ((i < arrayLength) && (timeArray[i] <= secs)) {
                DataPoint dp(timeArray[i],
                             (!hrArray.empty() ? hrArray[i] : 0),
                             (!wattsArray.empty() ? wattsArray[i] : 0),
                             (!npArray.empty() ? npArray[i] : 0),
                             (!apArray.empty() ? apArray[i] : 0),
                             (!xpArray.empty() ? xpArray[i] : 0),
                             (!speedArray.empty() ? speedArray[i] : 0),
                             (!cadArray.empty() ? cadArray[i] : 0),
                             (!altArray.empty() ? altArray[i] : 0),
                             (!tempArray.empty() ? tempArray[i] : 0),
                             (!windArray.empty() ? windArray[i] : 0),
                             (!torqueArray.empty() ? torqueArray[i] : 0),
                             (!balanceArray.empty() ? balanceArray[i] : 0));
                if (!wattsArray.empty())
                    totalWatts += wattsArray[i];
                if (!npArray.empty())
                    totalNP += npArray[i];
                if (!xpArray.empty())
                    totalXP += xpArray[i];
                if (!apArray.empty())
                    totalAP += apArray[i];
                if (!hrArray.empty())
                    totalHr    += hrArray[i];
                if (!speedArray.empty())
                    totalSpeed += speedArray[i];
                if (!cadArray.empty())
                    totalCad   += cadArray[i];
                if (!altArray.empty())
                    totalAlt   += altArray[i];
                if (!tempArray.empty() ) {
                    if (tempArray[i] == RideFile::noTemp) {
                        dp.temp = (i>0 && !list.empty()?list.back().temp:0.0);
                        totalTemp   += dp.temp;
                    }
                    else {
                        totalTemp   += tempArray[i];
                    }
                }
                if (!windArray.empty())
                    totalWind   += windArray[i];
                if (!torqueArray.empty())
                    totalTorque   += torqueArray[i];
                if (!balanceArray.empty())
                    totalBalance   += (balanceArray[i]>0?balanceArray[i]:50);

                totalDist   = distanceArray[i];
                list.append(dp);
                ++i;
            }
            while (!list.empty() && (list.front().time < secs - smooth)) {
                DataPoint &dp = list.front();
                totalWatts -= dp.watts;
                totalNP -= dp.np;
                totalAP -= dp.ap;
                totalXP -= dp.xp;
                totalHr    -= dp.hr;
                totalSpeed -= dp.speed;
                totalCad   -= dp.cad;
                totalAlt   -= dp.alt;
                totalTemp   -= dp.temp;
                totalWind   -= dp.wind;
                totalTorque   -= dp.torque;
                totalBalance   -= (dp.lrbalance>0?dp.lrbalance:50);
                list.removeFirst();
            }
            // TODO: this is wrong.  We should do a weighted average over the
            // seconds represented by each point...
            if (list.empty()) {
                smoothWatts[secs] = 0.0;
                smoothNP[secs] = 0.0;
                smoothXP[secs] = 0.0;
                smoothAP[secs] = 0.0;
                smoothHr[secs]    = 0.0;
                smoothSpeed[secs] = 0.0;
                smoothCad[secs]   = 0.0;
                smoothAltitude[secs]   = smoothAltitude[secs - 1];
                smoothTemp[secs]   = 0.0;
                smoothWind[secs] = 0.0;
                smoothRelSpeed[secs] =  QwtIntervalSample();
                smoothTorque[secs] = 0.0;
                smoothBalanceL[secs] = 50;
                smoothBalanceR[secs] = 50;
            }
            else {
                smoothWatts[secs]    = totalWatts / list.size();
                smoothNP[secs]    = totalNP / list.size();
                smoothXP[secs]    = totalXP / list.size();
                smoothAP[secs]    = totalAP / list.size();
                smoothHr[secs]       = totalHr / list.size();
                smoothSpeed[secs]    = totalSpeed / list.size();
                smoothCad[secs]      = totalCad / list.size();
                smoothAltitude[secs]      = totalAlt / list.size();
                smoothTemp[secs]      = totalTemp / list.size();
                smoothWind[secs]    = totalWind / list.size();
                smoothRelSpeed[secs] =  QwtIntervalSample( bydist ? totalDist : secs / 60.0, QwtInterval(qMin(totalWind / list.size(), totalSpeed / list.size()), qMax(totalWind / list.size(), totalSpeed / list.size()) ) );
                smoothTorque[secs]    = totalTorque / list.size();

                double balance = totalBalance / list.size();
                if (balance == 0) {
                    smoothBalanceL[secs]    = 50;
                    smoothBalanceR[secs]    = 50;
                } else if (balance >= 50) {
                    smoothBalanceL[secs]    = balance;
                    smoothBalanceR[secs]    = 50;
                }
                else {
                    smoothBalanceL[secs]    = 50;
                    smoothBalanceR[secs]    = balance;
                }
            }
            smoothDistance[secs] = totalDist;
            smoothTime[secs]  = secs / 60.0;
        }

    } else {

        // no smoothing .. just raw data
        smoothWatts.resize(0);
        smoothNP.resize(0);
        smoothXP.resize(0);
        smoothAP.resize(0);
        smoothHr.resize(0);
        smoothSpeed.resize(0);
        smoothCad.resize(0);
        smoothTime.resize(0);
        smoothDistance.resize(0);
        smoothAltitude.resize(0);
        smoothTemp.resize(0);
        smoothWind.resize(0);
        smoothRelSpeed.resize(0);
        smoothTorque.resize(0);
        smoothBalanceL.resize(0);
        smoothBalanceR.resize(0);

        foreach (RideFilePoint *dp, rideItem->ride()->dataPoints()) {
            smoothWatts.append(dp->watts);
            smoothNP.append(dp->np);
            smoothXP.append(dp->xp);
            smoothAP.append(dp->apower);
            smoothHr.append(dp->hr);
            smoothSpeed.append(context->athlete->useMetricUnits ? dp->kph : dp->kph * MILES_PER_KM);
            smoothCad.append(dp->cad);
            smoothTime.append(dp->secs/60);
            smoothDistance.append(context->athlete->useMetricUnits ? dp->km : dp->km * MILES_PER_KM);
            smoothAltitude.append(context->athlete->useMetricUnits ? dp->alt : dp->alt * FEET_PER_METER);
            smoothTemp.append(context->athlete->useMetricUnits ? dp->temp : dp->temp * FAHRENHEIT_PER_CENTIGRADE + FAHRENHEIT_ADD_CENTIGRADE);
            smoothWind.append(context->athlete->useMetricUnits ? dp->headwind : dp->headwind * MILES_PER_KM);
            smoothTorque.append(dp->nm);

            if (dp->lrbalance == 0) {
                smoothBalanceL.append(50);
                smoothBalanceR.append(50);
            }
            else if (dp->lrbalance >= 50) {
                smoothBalanceL.append(dp->lrbalance);
                smoothBalanceR.append(50);
            }
            else {
                smoothBalanceL.append(50);
                smoothBalanceR.append(dp->lrbalance);
            }

            double head = dp->headwind * (context->athlete->useMetricUnits ? 1.0f : MILES_PER_KM);
            double speed = dp->kph * (context->athlete->useMetricUnits ? 1.0f : MILES_PER_KM);
            smoothRelSpeed.append(QwtIntervalSample( bydist ? smoothDistance.last() : smoothTime.last(), QwtInterval(qMin(head, speed) , qMax(head, speed) ) ));

        }
    }

    QVector<double> &xaxis = bydist ? smoothDistance : smoothTime;
    int startingIndex = qMin(smooth, xaxis.count());
    int totalPoints = xaxis.count() - startingIndex;

    // set curves - we set the intervalHighlighter to whichver is available

    //W' curve set to whatever data we have
    wCurve->setData(parent->wpData->xdata().data(), parent->wpData->ydata().data(), parent->wpData->xdata().count());
    mCurve->setData(parent->wpData->mxdata().data(), parent->wpData->mydata().data(), parent->wpData->mxdata().count());

    if (!wattsArray.empty()) {
        wattsCurve->setData(xaxis.data() + startingIndex, smoothWatts.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft);
    }

    if (!npArray.empty()) {
        npCurve->setData(xaxis.data() + startingIndex, smoothNP.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft);
    }

    if (!xpArray.empty()) {
        xpCurve->setData(xaxis.data() + startingIndex, smoothXP.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft);
    }

    if (!apArray.empty()) {
        apCurve->setData(xaxis.data() + startingIndex, smoothAP.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft);
    }

    if (!hrArray.empty()) {
        hrCurve->setData(xaxis.data() + startingIndex, smoothHr.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft2);
    }

    if (!speedArray.empty()) {
        speedCurve->setData(xaxis.data() + startingIndex, smoothSpeed.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yRight);
    }

    if (!cadArray.empty()) {
        cadCurve->setData(xaxis.data() + startingIndex, smoothCad.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft2);
    }

    if (!altArray.empty()) {
        altCurve->setData(xaxis.data() + startingIndex, smoothAltitude.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yRight2);
    }

    if (!tempArray.empty()) {
        tempCurve->setData(xaxis.data() + startingIndex, smoothTemp.data() + startingIndex, totalPoints);
        if (context->athlete->useMetricUnits)
            intervalHighlighterCurve->setYAxis(yRight);
        else
            intervalHighlighterCurve->setYAxis(yLeft2);
    }


    if (!windArray.empty()) {
        windCurve->setData(new QwtIntervalSeriesData(smoothRelSpeed));
        intervalHighlighterCurve->setYAxis(yRight);
    }

    if (!torqueArray.empty()) {
        torqueCurve->setData(xaxis.data() + startingIndex, smoothTorque.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yRight);
    }

    if (!balanceArray.empty()) {
        balanceLCurve->setData(xaxis.data() + startingIndex, smoothBalanceL.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft2);
        balanceRCurve->setData(xaxis.data() + startingIndex, smoothBalanceR.data() + startingIndex, totalPoints);
        intervalHighlighterCurve->setYAxis(yLeft2);
    }

    setYMax();
    refreshReferenceLines();
    refreshIntervalMarkers();
    refreshCalibrationMarkers();
    refreshZoneLabels();

    //replot();
}

void
AllPlot::refreshIntervalMarkers()
{
    foreach(QwtPlotMarker *mrk, d_mrk) {
        mrk->detach();
        delete mrk;
    }
    d_mrk.clear();
    QRegExp wkoAuto("^(Peak *[0-9]*(s|min)|Entire workout|Find #[0-9]*) *\\([^)]*\\)$");
    if (rideItem->ride()) {
        foreach(const RideFileInterval &interval, rideItem->ride()->intervals()) {
            // skip WKO autogenerated peak intervals
            if (wkoAuto.exactMatch(interval.name))
                continue;
            QwtPlotMarker *mrk = new QwtPlotMarker;
            d_mrk.append(mrk);
            mrk->attach(this);
            mrk->setLineStyle(QwtPlotMarker::VLine);
            mrk->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
            mrk->setLinePen(QPen(GColor(CPLOTMARKER), 0, Qt::DashDotLine));

            // put matches on second line down
            QString name(interval.name);
            if (interval.name.startsWith(tr("Match"))) name = QString("\n%1").arg(interval.name);

            QwtText text(name);
            text.setFont(QFont("Helvetica", 10, QFont::Bold));
            if (interval.name.startsWith(tr("Match"))) 
                text.setColor(GColor(CWBAL));
            else
                text.setColor(GColor(CPLOTMARKER));
            if (!bydist)
                mrk->setValue(interval.start / 60.0, 0.0);
            else
                mrk->setValue((context->athlete->useMetricUnits ? 1 : MILES_PER_KM) *
                                rideItem->ride()->timeToDistance(interval.start), 0.0);
            mrk->setLabel(text);
        }
    }
}

void
AllPlot::refreshCalibrationMarkers()
{
    foreach(QwtPlotMarker *mrk, cal_mrk) {
        mrk->detach();
        delete mrk;
    }
    cal_mrk.clear();

    if (rideItem->ride()) {
        foreach(const RideFileCalibration &calibration, rideItem->ride()->calibrations()) {
            QwtPlotMarker *mrk = new QwtPlotMarker;
            cal_mrk.append(mrk);
            mrk->attach(this);
            mrk->setLineStyle(QwtPlotMarker::VLine);
            mrk->setLabelAlignment(Qt::AlignRight | Qt::AlignTop);
            mrk->setLinePen(QPen(GColor(CPLOTMARKER), 0, Qt::DashDotLine));
            QwtText text("\n\n"+calibration.name);
            text.setFont(QFont("Helvetica", 9, QFont::Bold));
            text.setColor(GColor(CPLOTMARKER));
            if (!bydist)
                mrk->setValue(calibration.start / 60.0, 0.0);
            else
                mrk->setValue((context->athlete->useMetricUnits ? 1 : MILES_PER_KM) *
                                rideItem->ride()->timeToDistance(calibration.start), 0.0);
            mrk->setLabel(text);
        }
    }
}

void
AllPlot::refreshReferenceLines()
{
    foreach(QwtPlotCurve *referenceLine, referenceLines) {
        referenceLine->detach();
        delete referenceLine;
    }
    referenceLines.clear();
    if (rideItem->ride()) {
        foreach(const RideFilePoint *referencePoint, rideItem->ride()->referencePoints()) {
            QwtPlotCurve *referenceLine = plotReferenceLine(referencePoint);
            referenceLines.append(referenceLine);
        }
    }
}

QwtPlotCurve*
AllPlot::plotReferenceLine(const RideFilePoint *referencePoint)
{
    QwtPlotCurve *referenceLine = NULL;

    QVector<double> xaxis;
    QVector<double> yaxis;
    if (bydist) {
        xaxis.append(referencePlot == NULL ? smoothDistance.first() : referencePlot->smoothDistance.first());
        xaxis.append(referencePlot == NULL ? smoothDistance.last() : referencePlot->smoothDistance.last());

    } else {
        xaxis.append(referencePlot == NULL ? smoothTime.first() : referencePlot->smoothTime.first());
        xaxis.append(referencePlot == NULL ? smoothTime.last() : referencePlot->smoothTime.last());
    }

    if (referencePoint->watts != 0)  {
        referenceLine = new QwtPlotCurve(tr("Power Ref"));
        referenceLine->setYAxis(yLeft);
        QPen wattsPen = QPen(GColor(CPOWER));
        wattsPen.setWidth(1);
        wattsPen.setStyle(Qt::DashLine);
        referenceLine->setPen(wattsPen);

        yaxis.append(referencePoint->watts);
        yaxis.append(referencePoint->watts);
    } else if (referencePoint->hr != 0)  {
        referenceLine = new QwtPlotCurve(tr("Heart Rate Ref"));
        referenceLine->setYAxis(yLeft);
        QPen hrPen = QPen(GColor(CHEARTRATE));
        hrPen.setWidth(1);
        hrPen.setStyle(Qt::DashLine);
        referenceLine->setPen(hrPen);

        yaxis.append(referencePoint->hr);
        yaxis.append(referencePoint->hr);
    } else if (referencePoint->cad != 0)  {
        referenceLine = new QwtPlotCurve(tr("Cadence Ref"));
        referenceLine->setYAxis(yLeft);
        QPen cadPen = QPen(GColor(CCADENCE));
        cadPen.setWidth(1);
        cadPen.setStyle(Qt::DashLine);
        referenceLine->setPen(cadPen);

        yaxis.append(referencePoint->cad);
        yaxis.append(referencePoint->cad);
    }

    if (referenceLine) {
        referenceLine->setData(xaxis,yaxis);
        referenceLine->attach(this);
        referenceLine->setVisible(true);
    }

    return referenceLine;
}


void
AllPlot::setYMax()
{
    // set axis scales
    if (wCurve->isVisible()) {

        setAxisTitle(yRight3, tr("W' Balance (j)"));
        setAxisScale(QwtPlot::yRight3,parent->wpData->minY-1000,parent->wpData->maxY+1000);
        setAxisLabelAlignment(yRight3,Qt::AlignVCenter);
    }

    if (wattsCurve->isVisible()) {
        double maxY = (referencePlot == NULL) ? (1.05 * wattsCurve->maxYValue()) :
                                             (1.05 * referencePlot->wattsCurve->maxYValue());

        int axisHeight = qRound( plotLayout()->canvasRect().height() );
        QFontMetrics labelWidthMetric = QFontMetrics( QwtPlot::axisFont(yLeft) );
        int labelWidth = labelWidthMetric.width( (maxY > 1000) ? " 8,888 " : " 888 " );

        int step = 100;
        while( ( qCeil(maxY / step) * labelWidth ) > axisHeight )
        {
            nextStep(step);
        }

        QwtValueList xytick[QwtScaleDiv::NTickTypes];
        for (int i=0;i<maxY && i<2000;i+=step)
            xytick[QwtScaleDiv::MajorTick]<<i;

        setAxisTitle(yLeft, tr("Watts"));
        setAxisScaleDiv(QwtPlot::yLeft,QwtScaleDiv(0.0,maxY,xytick));
        //setAxisLabelAlignment(yLeft,Qt::AlignVCenter);
    }
    if (hrCurve->isVisible() || cadCurve->isVisible() || (!context->athlete->useMetricUnits && tempCurve->isVisible()) || balanceLCurve->isVisible()) {
        double ymin = 0;
        double ymax = 0;

        QStringList labels;
        if (hrCurve->isVisible()) {
            labels << tr("BPM");
            if (referencePlot == NULL)
                ymax = hrCurve->maxYValue();
            else
                ymax = referencePlot->hrCurve->maxYValue();
        }
        if (cadCurve->isVisible()) {
            labels << tr("RPM");
            if (referencePlot == NULL)
                ymax = qMax(ymax, cadCurve->maxYValue());
            else
                ymax = qMax(ymax, referencePlot->cadCurve->maxYValue());
        }
        if (tempCurve->isVisible() && !context->athlete->useMetricUnits) {

            labels << QString::fromUtf8("°F");

            if (referencePlot == NULL) {
                ymin = qMin(ymin, tempCurve->minYValue());
                ymax = qMax(ymax, tempCurve->maxYValue());
            }
            else {
                ymin = qMin(ymin, referencePlot->tempCurve->minYValue());
                ymax = qMax(ymax, referencePlot->tempCurve->maxYValue());
            }
        }
        if (balanceLCurve->isVisible()) {
            labels << tr("% left");
            if (referencePlot == NULL)
                ymax = qMax(ymax, balanceLCurve->maxYValue());
            else
                ymax = qMax(ymax, referencePlot->balanceLCurve->maxYValue());

            balanceLCurve->setBaseline(50);
            balanceRCurve->setBaseline(50);
        }

        int axisHeight = qRound( plotLayout()->canvasRect().height() );
        QFontMetrics labelWidthMetric = QFontMetrics( QwtPlot::axisFont(yLeft) );
        int labelWidth = labelWidthMetric.width( "888 " );

        ymax *= 1.05;
        int step = 10;
        while( ( qCeil(ymax / step) * labelWidth ) > axisHeight )
        {
            nextStep(step);
        }

        QwtValueList xytick[QwtScaleDiv::NTickTypes];
        for (int i=0;i<ymax;i+=step)
            xytick[QwtScaleDiv::MajorTick]<<i;

        setAxisTitle(yLeft2, labels.join(" / "));
        setAxisScaleDiv(yLeft2,QwtScaleDiv(ymin, ymax, xytick));
        //setAxisLabelAlignment(yLeft2,Qt::AlignVCenter);
    }
    if (speedCurve->isVisible() || (context->athlete->useMetricUnits && tempCurve->isVisible()) || torqueCurve->isVisible()) {
        double ymin = 0;
        double ymax = 0;

        QStringList labels;

        if (speedCurve->isVisible()) {
            labels << (context->athlete->useMetricUnits ? tr("KPH") : tr("MPH"));

            if (referencePlot == NULL)
                ymax = speedCurve->maxYValue();
            else
                ymax = referencePlot->speedCurve->maxYValue();
        }
        if (tempCurve->isVisible() && context->athlete->useMetricUnits) {

            labels << QString::fromUtf8("°C");

            if (referencePlot == NULL) {
                ymin = qMin(ymin, tempCurve->minYValue());
                ymax = qMax(ymax, tempCurve->maxYValue());
            }
            else {
                ymin = qMin(ymin, referencePlot->tempCurve->minYValue());
                ymax = qMax(ymax, referencePlot->tempCurve->maxYValue());
            }
        }
        if (torqueCurve->isVisible()) {
            labels << (context->athlete->useMetricUnits ? tr("Nm") : tr("ftLb"));

            if (referencePlot == NULL)
                ymax = qMax(ymax, torqueCurve->maxYValue());
            else
                ymax = qMax(ymax, referencePlot->torqueCurve->maxYValue());
        }
        setAxisTitle(yRight, labels.join(" / "));
        setAxisScale(yRight, ymin, 1.05 * ymax);
        //setAxisLabelAlignment(yRight,Qt::AlignVCenter);
    }
    if (altCurve->isVisible()) {
        setAxisTitle(yRight2, context->athlete->useMetricUnits ? tr("Meters") : tr("Feet"));
        double ymin,ymax;

        if (referencePlot == NULL) {
            ymin = altCurve->minYValue();
            ymax = qMax(ymin + 100, 1.05 * altCurve->maxYValue());
        } else {
            ymin = referencePlot->altCurve->minYValue();
            ymax = qMax(ymin + 100, 1.05 * referencePlot->altCurve->maxYValue());
        }
        ymin = (ymin < 0 ? -100 : 0) + ( qRound(ymin) / 100 ) * 100;

        int axisHeight = qRound( plotLayout()->canvasRect().height() );
        QFontMetrics labelWidthMetric = QFontMetrics( QwtPlot::axisFont(yLeft) );
        int labelWidth = labelWidthMetric.width( (ymax > 1000) ? " 8888 " : " 888 " );

        int step = 10;
        while( ( qCeil( (ymax - ymin ) / step) * labelWidth ) > axisHeight )
        {
            nextStep(step);
        }

        QwtValueList xytick[QwtScaleDiv::NTickTypes];
        for (int i=ymin;i<ymax;i+=step)
            xytick[QwtScaleDiv::MajorTick]<<i;

        //setAxisScale(yRight2, ymin, ymax);
        setAxisScaleDiv(yRight2,QwtScaleDiv(ymin,ymax,xytick));
        //setAxisLabelAlignment(yRight2,Qt::AlignVCenter);
        altCurve->setBaseline(ymin);
    }

    enableAxis(yLeft, wattsCurve->isVisible() || npCurve->isVisible() || xpCurve->isVisible() || apCurve->isVisible());
    enableAxis(yLeft2, hrCurve->isVisible() || cadCurve->isVisible());
    enableAxis(yRight, speedCurve->isVisible());
    enableAxis(yRight2, altCurve->isVisible());
    enableAxis(yRight3, wCurve->isVisible());
}

void
AllPlot::setXTitle()
{
    if (bydist)
        setAxisTitle(xBottom, context->athlete->useMetricUnits ? "KM" : "Miles");
    else
        setAxisTitle(xBottom, tr("")); // time is bloody obvious, less noise
}

void
AllPlot::setDataFromPlot(AllPlot *plot, int startidx, int stopidx)
{
    if (plot == NULL) {
        rideItem = NULL;
        return;
    }

    referencePlot = plot;

    // You got to give me some data first!
    if (!plot->distanceArray.count() || !plot->timeArray.count()) return;

    // reference the plot for data and state
    rideItem = plot->rideItem;
    bydist = plot->bydist;

    arrayLength = stopidx-startidx;

    if (bydist) {
        startidx = plot->distanceIndex(plot->distanceArray[startidx]);
        stopidx  = plot->distanceIndex(plot->distanceArray[(stopidx>=plot->distanceArray.size()?plot->distanceArray.size()-1:stopidx)])-1;
    } else {
        startidx = plot->timeIndex(plot->timeArray[startidx]/60);
        stopidx  = plot->timeIndex(plot->timeArray[(stopidx>=plot->timeArray.size()?plot->timeArray.size()-1:stopidx)]/60)-1;
    }

    // center the curve title
    curveTitle.setYValue(30);
    curveTitle.setXValue(2);

    // make sure indexes are still valid
    if (startidx > stopidx || startidx < 0 || stopidx < 0) return;

    double *smoothW = &plot->smoothWatts[startidx];
    double *smoothN = &plot->smoothNP[startidx];
    double *smoothX = &plot->smoothXP[startidx];
    double *smoothL = &plot->smoothAP[startidx];
    double *smoothT = &plot->smoothTime[startidx];
    double *smoothHR = &plot->smoothHr[startidx];
    double *smoothS = &plot->smoothSpeed[startidx];
    double *smoothC = &plot->smoothCad[startidx];
    double *smoothA = &plot->smoothAltitude[startidx];
    double *smoothD = &plot->smoothDistance[startidx];
    double *smoothTE = &plot->smoothTemp[startidx];
    //double *smoothWND = &plot->smoothWind[startidx];
    double *smoothNM = &plot->smoothTorque[startidx];
    double *smoothBALL = &plot->smoothBalanceL[startidx];
    double *smoothBALR = &plot->smoothBalanceR[startidx];

    QwtIntervalSample *smoothRS = &plot->smoothRelSpeed[startidx];

    double *xaxis = bydist ? smoothD : smoothT;

    // attach appropriate curves
    //if (this->legend()) this->legend()->hide();
    if (showW && parent->wpData->TAU > 0) {

        // matches cost
        double burnt=0;
        int count=0;
        foreach(struct Match match, parent->wpData->matches)
            if (match.cost > 2000) { //XXX how to decide the threshold for a match?
                burnt += match.cost;
                count++;
            }

        QwtText text(QString("Tau=%1, CP=%2, W'=%3, %4 matches >2kJ (%5 kJ)").arg(parent->wpData->TAU)
                                                    .arg(parent->wpData->CP)
                                                    .arg(parent->wpData->WPRIME)
                                                    .arg(count)
                                                    .arg(burnt/1000.00, 0, 'f', 1));

        text.setFont(QFont("Helvetica", 10, QFont::Bold));
        text.setColor(GColor(CWBAL));
        curveTitle.setLabel(text);
    } else {
        curveTitle.setLabel(QwtText(""));
    }

    wCurve->detach();
    mCurve->detach();
    wattsCurve->detach();
    npCurve->detach();
    xpCurve->detach();
    apCurve->detach();
    hrCurve->detach();
    speedCurve->detach();
    cadCurve->detach();
    altCurve->detach();
    tempCurve->detach();
    windCurve->detach();
    torqueCurve->detach();
    balanceLCurve->detach();
    balanceRCurve->detach();

    wattsCurve->setVisible(rideItem->ride()->areDataPresent()->watts && showPowerState < 2);
    npCurve->setVisible(rideItem->ride()->areDataPresent()->np && showNP);
    xpCurve->setVisible(rideItem->ride()->areDataPresent()->xp && showXP);
    apCurve->setVisible(rideItem->ride()->areDataPresent()->apower && showAP);
    wCurve->setVisible(rideItem->ride()->areDataPresent()->watts && showPowerState<2 && showW);
    mCurve->setVisible(rideItem->ride()->areDataPresent()->watts && showPowerState<2 && showW);
    hrCurve->setVisible(rideItem->ride()->areDataPresent()->hr && showHr);
    speedCurve->setVisible(rideItem->ride()->areDataPresent()->kph && showSpeed);
    cadCurve->setVisible(rideItem->ride()->areDataPresent()->cad && showCad);
    altCurve->setVisible(rideItem->ride()->areDataPresent()->alt && showAlt);
    tempCurve->setVisible(rideItem->ride()->areDataPresent()->temp && showTemp);
    windCurve->setVisible(rideItem->ride()->areDataPresent()->headwind && showWind);
    torqueCurve->setVisible(rideItem->ride()->areDataPresent()->nm && showTorque);
    balanceLCurve->setVisible(rideItem->ride()->areDataPresent()->lrbalance && showBalance);
    balanceRCurve->setVisible(rideItem->ride()->areDataPresent()->lrbalance && showBalance);

    wCurve->setData(parent->wpData->xdata().data(),parent->wpData->ydata().data(),parent->wpData->xdata().count());
    mCurve->setData(parent->wpData->mxdata().data(),parent->wpData->mydata().data(),parent->wpData->mxdata().count());
    wattsCurve->setData(xaxis,smoothW,stopidx-startidx);
    npCurve->setData(xaxis,smoothN,stopidx-startidx);
    xpCurve->setData(xaxis,smoothX,stopidx-startidx);
    apCurve->setData(xaxis,smoothL,stopidx-startidx);
    hrCurve->setData(xaxis, smoothHR,stopidx-startidx);
    speedCurve->setData(xaxis, smoothS, stopidx-startidx);
    cadCurve->setData(xaxis, smoothC, stopidx-startidx);
    altCurve->setData(xaxis, smoothA, stopidx-startidx);
    tempCurve->setData(xaxis, smoothTE, stopidx-startidx);

    QVector<QwtIntervalSample> tmpWND(stopidx-startidx);
    qMemCopy( tmpWND.data(), smoothRS, (stopidx-startidx) * sizeof( QwtIntervalSample ) );
    windCurve->setData(new QwtIntervalSeriesData(tmpWND));
    torqueCurve->setData(xaxis, smoothNM, stopidx-startidx);
    balanceLCurve->setData(xaxis, smoothBALL, stopidx-startidx);
    balanceRCurve->setData(xaxis, smoothBALR, stopidx-startidx);

    /*QVector<double> _time(stopidx-startidx);
    qMemCopy( _time.data(), xaxis, (stopidx-startidx) * sizeof( double ) );

    QVector<QwtIntervalSample> tmpWND(stopidx-startidx);
    for (int i=0;i<_time.count();i++) {
        QwtIntervalSample inter = QwtIntervalSample(_time.at(i), 20,50);
        tmpWND.append(inter); // plot->smoothRelSpeed.at(i)
    }*/

    QwtSymbol sym;
    sym.setPen(QPen(GColor(CPLOTMARKER)));
    if (stopidx-startidx < 150) {
        sym.setStyle(QwtSymbol::Ellipse);
        sym.setSize(3);
    } else {
        sym.setStyle(QwtSymbol::NoSymbol);
        sym.setSize(0);
    }

    wCurve->setSymbol(new QwtSymbol(sym));
    wattsCurve->setSymbol(new QwtSymbol(sym));
    npCurve->setSymbol(new QwtSymbol(sym));
    xpCurve->setSymbol(new QwtSymbol(sym));
    apCurve->setSymbol(new QwtSymbol(sym));
    hrCurve->setSymbol(new QwtSymbol(sym));
    speedCurve->setSymbol(new QwtSymbol(sym));
    cadCurve->setSymbol(new QwtSymbol(sym));
    altCurve->setSymbol(new QwtSymbol(sym));
    tempCurve->setSymbol(new QwtSymbol(sym));
    torqueCurve->setSymbol(new QwtSymbol(sym));
    balanceLCurve->setSymbol(new QwtSymbol(sym));
    balanceRCurve->setSymbol(new QwtSymbol(sym));

    setYMax();

    setAxisScale(xBottom, xaxis[0], xaxis[stopidx-startidx-1]);

    if (!plot->smoothAltitude.empty()) {
        altCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yRight2);
    }
    if (parent->wpData->xdata().count()) {
        wCurve->attach(this);
        mCurve->attach(this);
    }
    if (!plot->smoothWatts.empty()) {
        wattsCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yLeft);
    }
    if (!plot->smoothNP.empty()) {
        npCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yLeft);
    }
    if (!plot->smoothXP.empty()) {
        xpCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yLeft);
    }
    if (!plot->smoothAP.empty()) {
        apCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yLeft);
    }
    if (!plot->smoothHr.empty()) {
        hrCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yLeft2);
    }
    if (!plot->smoothSpeed.empty()) {
        speedCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yRight);
    }
    if (!plot->smoothCad.empty()) {
        cadCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yLeft2);
    }
    if (!plot->smoothTemp.empty()) {
        tempCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yRight);
    }
    if (!plot->smoothWind.empty()) {
        windCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yRight);
    }
    if (!plot->smoothTorque.empty()) {
        torqueCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yRight);
    }
    if (!plot->smoothBalanceL.empty()) {
        balanceLCurve->attach(this);
        balanceRCurve->attach(this);
        intervalHighlighterCurve->setYAxis(yLeft2);
    }


    refreshReferenceLines();
    refreshIntervalMarkers();
    refreshCalibrationMarkers();
    refreshZoneLabels();

    //if (this->legend()) this->legend()->show();
    //replot();
}

void
AllPlot::setDataFromRide(RideItem *_rideItem)
{
    rideItem = _rideItem;
    if (_rideItem == NULL) return;

    wattsArray.clear();
    curveTitle.setLabel(QwtText(QString(""), QwtText::PlainText)); // default to no title

    RideFile *ride = rideItem->ride();
    if (ride && ride->dataPoints().size()) {
        const RideFileDataPresent *dataPresent = ride->areDataPresent();
        int npoints = ride->dataPoints().size();
        wattsArray.resize(dataPresent->watts ? npoints : 0);
        npArray.resize(dataPresent->np ? npoints : 0);
        xpArray.resize(dataPresent->xp ? npoints : 0);
        apArray.resize(dataPresent->apower ? npoints : 0);
        hrArray.resize(dataPresent->hr ? npoints : 0);
        speedArray.resize(dataPresent->kph ? npoints : 0);
        cadArray.resize(dataPresent->cad ? npoints : 0);
        altArray.resize(dataPresent->alt ? npoints : 0);
        tempArray.resize(dataPresent->temp ? npoints : 0);
        windArray.resize(dataPresent->headwind ? npoints : 0);
        torqueArray.resize(dataPresent->nm ? npoints : 0);
        balanceArray.resize(dataPresent->lrbalance ? npoints : 0);
        timeArray.resize(npoints);
        distanceArray.resize(npoints);

        // attach appropriate curves
        wCurve->detach();
        mCurve->detach();
        wattsCurve->detach();
        npCurve->detach();
        xpCurve->detach();
        apCurve->detach();
        hrCurve->detach();
        speedCurve->detach();
        cadCurve->detach();
        altCurve->detach();
        tempCurve->detach();
        windCurve->detach();
        torqueCurve->detach();
        balanceLCurve->detach();
        balanceRCurve->detach();

        if (!altArray.empty()) altCurve->attach(this);
        if (!wattsArray.empty()) wattsCurve->attach(this);
        if (!npArray.empty()) npCurve->attach(this);
        if (!xpArray.empty()) xpCurve->attach(this);
        if (!apArray.empty()) apCurve->attach(this);
        if (!parent->wpData->ydata().empty()) {
            wCurve->attach(this);
            mCurve->attach(this);
        }
        if (!hrArray.empty()) hrCurve->attach(this);
        if (!speedArray.empty()) speedCurve->attach(this);
        if (!cadArray.empty()) cadCurve->attach(this);
        if (!tempArray.empty()) tempCurve->attach(this);
        if (!windArray.empty()) windCurve->attach(this);
        if (!torqueArray.empty()) torqueCurve->attach(this);
        if (!balanceArray.empty()) {
            balanceLCurve->attach(this);
            balanceRCurve->attach(this);
        }

        wCurve->setVisible(dataPresent->watts && showPowerState < 2 && showW);
        mCurve->setVisible(dataPresent->watts && showPowerState < 2 && showW);
        wattsCurve->setVisible(dataPresent->watts && showPowerState < 2);
        npCurve->setVisible(dataPresent->np && showNP);
        xpCurve->setVisible(dataPresent->xp && showXP);
        apCurve->setVisible(dataPresent->apower && showAP);
        hrCurve->setVisible(dataPresent->hr && showHr);
        speedCurve->setVisible(dataPresent->kph && showSpeed);
        cadCurve->setVisible(dataPresent->cad && showCad);
        altCurve->setVisible(dataPresent->alt && showAlt);
        tempCurve->setVisible(dataPresent->temp && showTemp);
        windCurve->setVisible(dataPresent->headwind && showWind);
        torqueCurve->setVisible(dataPresent->nm && showWind);
        balanceLCurve->setVisible(dataPresent->lrbalance && showBalance);
        balanceRCurve->setVisible(dataPresent->lrbalance && showBalance);

        arrayLength = 0;
        foreach (const RideFilePoint *point, ride->dataPoints()) {

            // we round the time to nearest 100th of a second
            // before adding to the array, to avoid situation
            // where 'high precision' time slice is an artefact
            // of double precision or slight timing anomalies
            // e.g. where realtime gives timestamps like
            // 940.002 followed by 940.998 and were previouslt
            // both rounded to 940s
            //
            // NOTE: this rounding mechanism is identical to that
            //       used by the Ride Editor.
            double secs = floor(point->secs);
            double msecs = round((point->secs - secs) * 100) * 10;

            timeArray[arrayLength]  = secs + msecs/1000;
            if (!wattsArray.empty()) wattsArray[arrayLength] = max(0, point->watts);
            if (!npArray.empty()) npArray[arrayLength] = max(0, point->np);
            if (!xpArray.empty()) xpArray[arrayLength] = max(0, point->xp);
            if (!apArray.empty()) apArray[arrayLength] = max(0, point->apower);

            if (!hrArray.empty())
                hrArray[arrayLength]    = max(0, point->hr);
            if (!speedArray.empty())
                speedArray[arrayLength] = max(0,
                                              (context->athlete->useMetricUnits
                                               ? point->kph
                                               : point->kph * MILES_PER_KM));
            if (!cadArray.empty())
                cadArray[arrayLength]   = max(0, point->cad);
            if (!altArray.empty())
                altArray[arrayLength]   = (context->athlete->useMetricUnits
                                           ? point->alt
                                           : point->alt * FEET_PER_METER);
            if (!tempArray.empty())
                tempArray[arrayLength]   = point->temp;

            if (!windArray.empty())
                windArray[arrayLength] = max(0,
                                             (context->athlete->useMetricUnits
                                              ? point->headwind
                                              : point->headwind * MILES_PER_KM));

            if (!balanceArray.empty())
                balanceArray[arrayLength]   = point->lrbalance;

            distanceArray[arrayLength] = max(0,
                                             (context->athlete->useMetricUnits
                                              ? point->km
                                              : point->km * MILES_PER_KM));

            if (!torqueArray.empty())
                torqueArray[arrayLength] = max(0,
                                              (context->athlete->useMetricUnits
                                               ? point->nm
                                               : point->nm * FEET_LB_PER_NM));
            ++arrayLength;
        }
        recalc();
    }
    else {
        //setTitle("no data");

        wCurve->detach();
        mCurve->detach();
        wattsCurve->detach();
        npCurve->detach();
        xpCurve->detach();
        apCurve->detach();
        hrCurve->detach();
        speedCurve->detach();
        cadCurve->detach();
        altCurve->detach();
        tempCurve->detach();
        windCurve->detach();
        torqueCurve->detach();
        balanceLCurve->detach();
        balanceRCurve->detach();

        foreach(QwtPlotMarker *mrk, d_mrk)
            delete mrk;
        d_mrk.clear();

        foreach(QwtPlotMarker *mrk, cal_mrk)
            delete mrk;
        cal_mrk.clear();

        foreach(QwtPlotCurve *referenceLine, referenceLines) {
            referenceLine->detach();
            delete referenceLine;
        }
        referenceLines.clear();
    }
}

void
AllPlot::setShowPower(int id)
{
    if (showPowerState == id) return;

    showPowerState = id;
    wattsCurve->setVisible(id < 2);
    shade_zones = (id == 0);
    setYMax();
    if (shade_zones) {
        bg->attach(this);
        refreshZoneLabels();
    } else
        bg->detach();
}

void
AllPlot::setShowNP(bool show)
{
    showNP = show;
    npCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowXP(bool show)
{
    showXP = show;
    xpCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowAP(bool show)
{
    showAP = show;
    apCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowHr(bool show)
{
    showHr = show;
    hrCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowSpeed(bool show)
{
    showSpeed = show;
    speedCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowCad(bool show)
{
    showCad = show;
    cadCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowAlt(bool show)
{
    showAlt = show;
    altCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowTemp(bool show)
{
    showTemp = show;
    tempCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowWind(bool show)
{
    showWind = show;
    windCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowW(bool show)
{
    showW = show;
    wCurve->setVisible(show);
    mCurve->setVisible(show);
    if (!showW || parent->wpData->TAU <= 0) {
        curveTitle.setLabel(QwtText(""));
    }
    setYMax();
    replot();
}

void
AllPlot::setShowTorque(bool show)
{
    showTorque = show;
    torqueCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowBalance(bool show)
{
    showBalance = show;
    balanceLCurve->setVisible(show);
    balanceRCurve->setVisible(show);
    setYMax();
    replot();
}

void
AllPlot::setShowGrid(bool show)
{
    grid->setVisible(show);
    replot();
}

void
AllPlot::setPaintBrush(int state)
{
    if (state) {

        QColor p;
        p = wCurve->pen().color();
        p.setAlpha(64);
        wCurve->setBrush(QBrush(p));

        p = wattsCurve->pen().color();
        p.setAlpha(64);
        wattsCurve->setBrush(QBrush(p));

        p = npCurve->pen().color();
        p.setAlpha(64);
        npCurve->setBrush(QBrush(p));

        p = xpCurve->pen().color();
        p.setAlpha(64);
        xpCurve->setBrush(QBrush(p));

        p = apCurve->pen().color();
        p.setAlpha(64);
        apCurve->setBrush(QBrush(p));

        p = hrCurve->pen().color();
        p.setAlpha(64);
        hrCurve->setBrush(QBrush(p));

        p = speedCurve->pen().color();
        p.setAlpha(64);
        speedCurve->setBrush(QBrush(p));

        p = cadCurve->pen().color();
        p.setAlpha(64);
        cadCurve->setBrush(QBrush(p));

        p = tempCurve->pen().color();
        p.setAlpha(64);
        tempCurve->setBrush(QBrush(p));

        p = torqueCurve->pen().color();
        p.setAlpha(64);
        torqueCurve->setBrush(QBrush(p));

        /*p = balanceLCurve->pen().color();
        p.setAlpha(64);
        balanceLCurve->setBrush(QBrush(p));

        p = balanceRCurve->pen().color();
        p.setAlpha(64);
        balanceRCurve->setBrush(QBrush(p));*/
    } else {
        wCurve->setBrush(Qt::NoBrush);
        wattsCurve->setBrush(Qt::NoBrush);
        npCurve->setBrush(Qt::NoBrush);
        xpCurve->setBrush(Qt::NoBrush);
        apCurve->setBrush(Qt::NoBrush);
        hrCurve->setBrush(Qt::NoBrush);
        speedCurve->setBrush(Qt::NoBrush);
        cadCurve->setBrush(Qt::NoBrush);
        tempCurve->setBrush(Qt::NoBrush);
        torqueCurve->setBrush(Qt::NoBrush);
        //balanceLCurve->setBrush(Qt::NoBrush);
        //balanceRCurve->setBrush(Qt::NoBrush);
    }
    replot();
}

void
AllPlot::setSmoothing(int value)
{
    smooth = value;
    recalc();
}

void
AllPlot::setByDistance(int id)
{
    bydist = (id == 1);
    setXTitle();
    recalc();
}

struct ComparePoints {
    bool operator()(const double p1, const double p2) {
        return p1 < p2;
    }
};

int
AllPlot::timeIndex(double min) const
{
    // return index offset for specified time
    QVector<double>::const_iterator i = std::lower_bound(
            smoothTime.begin(), smoothTime.end(), min, ComparePoints());
    if (i == smoothTime.end())
        return smoothTime.size();
    return i - smoothTime.begin();
}



int
AllPlot::distanceIndex(double km) const
{
    // return index offset for specified distance in km
	QVector<double>::const_iterator i = std::lower_bound(
            smoothDistance.begin(), smoothDistance.end(), km, ComparePoints());
    if (i == smoothDistance.end())
        return smoothDistance.size();
    return i - smoothDistance.begin();
}
/*----------------------------------------------------------------------
 * Interval plotting
 *--------------------------------------------------------------------*/


/*
 * HELPER FUNCTIONS:
 *    intervalNum - returns a pointer to the nth selected interval
 *    intervalCount - returns the number of highlighted intervals
 */

// note this is operating on the children of allIntervals and not the
// intervalWidget (QTreeWidget) -- this is why we do not use the
// selectedItems() member. N starts a one not zero.
IntervalItem *IntervalPlotData::intervalNum(int n) const
{
    int highlighted=0;
    const QTreeWidgetItem *allIntervals = context->athlete->allIntervalItems();
    for (int i=0; i<allIntervals->childCount(); i++) {
        IntervalItem *current = (IntervalItem *)allIntervals->child(i);

        if (current != NULL) {
            if (current->isSelected() == true) ++highlighted;
        } else {
            return NULL;
        }
        if (highlighted == n) return current;
    }
    return NULL;
}

// how many intervals selected?
int IntervalPlotData::intervalCount() const
{
    int highlighted;
    highlighted = 0;
    if (context->athlete->allIntervalItems() == NULL) return 0; // not inited yet!

    const QTreeWidgetItem *allIntervals = context->athlete->allIntervalItems();
    for (int i=0; i<allIntervals->childCount(); i++) {
        IntervalItem *current = (IntervalItem *)allIntervals->child(i);
        if (current != NULL) {
            if (current->isSelected() == true) {
                ++highlighted;
            }
        }
    }
    return highlighted;
}

/*
 * INTERVAL HIGHLIGHTING CURVE
 * IntervalPlotData - implements the qwtdata interface where
 *                    x,y return point co-ordinates and
 *                    size returns the number of points
 */

// The interval curve data is derived from the intervals that have
// been selected in the Context leftlayout for each selected
// interval we return 4 data points; bottomleft, topleft, topright
// and bottom right.
//
// the points correspond to:
// bottom left = interval start, 0 watts
// top left = interval start, maxwatts
// top right = interval stop, maxwatts
// bottom right = interval stop, 0 watts
//
double IntervalPlotData::x(size_t i) const
{
    // for each interval there are four points, which interval is this for?
    int interval = i ? i/4 : 0;
    interval += 1; // interval numbers start at 1 not ZERO in the utility functions

    double multiplier = context->athlete->useMetricUnits ? 1 : MILES_PER_KM;

    // get the interval
    IntervalItem *current = intervalNum(interval);
    if (current == NULL) return 0; // out of bounds !?

    // which point are we returning?
    switch (i%4) {
    case 0 : return allPlot->bydist ? multiplier * current->startKM : current->start/60; // bottom left
    case 1 : return allPlot->bydist ? multiplier * current->startKM : current->start/60; // top left
    case 2 : return allPlot->bydist ? multiplier * current->stopKM : current->stop/60; // bottom right
    case 3 : return allPlot->bydist ? multiplier * current->stopKM : current->stop/60; // bottom right
    }
    return 0; // shouldn't get here, but keeps compiler happy
}


double IntervalPlotData::y(size_t i) const
{
    // which point are we returning?
    switch (i%4) {
    case 0 : return -100; // bottom left
    case 1 : return 5000; // top left - set to out of bound value
    case 2 : return 5000; // top right - set to out of bound value
    case 3 : return -100;  // bottom right
    }
    return 0;
}


size_t IntervalPlotData::size() const { return intervalCount()*4; }

QPointF IntervalPlotData::sample(size_t i) const {
    return QPointF(x(i), y(i));
}

QRectF IntervalPlotData::boundingRect() const
{
    return QRectF(-100, 5000, 5100, 5100);
}

void
AllPlot::pointHover(QwtPlotCurve *curve, int index)
{
    if (index >= 0 && curve != intervalHighlighterCurve) {

        double yvalue = curve->sample(index).y();
        double xvalue = curve->sample(index).x();

        QString xstring;
        if (bydist) {
            xstring = QString("%1").arg(xvalue);
        } else {
            QTime t = QTime().addSecs(xvalue*60.00);
            xstring = t.toString("hh:mm:ss");
        }

        // output the tooltip
        QString text = QString("%1 %2\n%3 %4")
                        .arg(yvalue, 0, 'f', 0)
                        .arg(this->axisTitle(curve->yAxis()).text())
                        .arg(xstring)
                        .arg(this->axisTitle(curve->xAxis()).text());

        // set that text up
        tooltip->setText(text);

    } else {

        // no point
        tooltip->setText("");
    }
}

void
AllPlot::nextStep( int& step )
{
    if( step < 50 )
    {
        step += 10;
    }
    else if( step == 50 )
    {
        step = 100;
    }
    else if( step >= 100 && step < 1000 )
    {
        step += 100;
    }
    else if( step >= 1000 && step < 5000)
    {
        step += 500;
    }
    else
    {
        step += 1000;
    }
}

bool
AllPlot::eventFilter(QObject *obj, QEvent *event)
{
    int axis = -1;
    if (obj == axisWidget(QwtPlot::yLeft))
        axis=QwtPlot::yLeft;
    if (obj == axisWidget(QwtPlot::yLeft1))
        axis=QwtPlot::yLeft1;

    if (axis>-1 && event->type() == QEvent::MouseButtonDblClick) {
        QMouseEvent *m = static_cast<QMouseEvent*>(event);
        confirmTmpReference(invTransform(axis, m->y()),axis, true); // do show delete stuff
    }
    if (axis>-1 && event->type() == QEvent::MouseMove) {
        QMouseEvent *m = static_cast<QMouseEvent*>(event);
        plotTmpReference(axis, m->x()-axisWidget(axis)->width(), m->y());
    }
    if (axis>-1 && event->type() == QEvent::MouseButtonRelease) {
        QMouseEvent *m = static_cast<QMouseEvent*>(event);
        if (m->x()>axisWidget(axis)->width()) {
            confirmTmpReference(invTransform(axis, m->y()),axis,false); // don't show delete stuff
        }
        else  {
            plotTmpReference(axis, 0, 0); //unplot
        }
    }
    return false;
}

void
AllPlot::plotTmpReference(int axis, int x, int y)
{
    if (x>0) {
        RideFilePoint *refPoint = new RideFilePoint();
        refPoint->watts = invTransform(axis, y);

        foreach(QwtPlotCurve *curve, tmpReferenceLines) {
            if (curve) {
                curve->detach();
                delete curve;
            }
        }
        tmpReferenceLines.clear();

        tmpReferenceLines.append(parent->allPlot->plotReferenceLine(refPoint));
        parent->allPlot->replot();
        foreach(AllPlot *plot, parent->allPlots) {
            tmpReferenceLines.append(plot->plotReferenceLine(refPoint));
            plot->replot();
        }
    } else  {
        foreach(QwtPlotCurve *curve, tmpReferenceLines) {
            if (curve) {
                curve->detach();
                delete curve;
            }
        }
        tmpReferenceLines.clear();
        parent->allPlot->replot();
        foreach(AllPlot *plot, parent->allPlots) {
            plot->replot();
        }
    }
}

void
AllPlot::refreshReferenceLinesForAllPlots()
{
    parent->allPlot->refreshReferenceLines();
    foreach(AllPlot *plot, parent->allPlots) {
        plot->refreshReferenceLines();
    }
}

void
AllPlot::confirmTmpReference(double value, int axis, bool allowDelete)
{
    ReferenceLineDialog *p = new ReferenceLineDialog(this, context, allowDelete);
    p->setWindowModality(Qt::ApplicationModal); // don't allow select other ride or it all goes wrong!
    p->setValueForAxis(value, axis);
    p->move(QCursor::pos()-QPoint(40,40));
    p->exec();
}

/*
 *   Copyright 2005 by Aaron Seigo <aseigo@kde.org>
 *   Copyright 2007 by Riccardo Iaconelli <riccardo@kde.org>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "applet.h"

#include <cmath>
#include <limits>

#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QList>
#include <QPainter>
#include <QSize>
#include <QStyleOptionGraphicsItem>
#include <QTextDocument>
#include <QTimer>
#include <QUiLoader>
#include <QGraphicsSceneMouseEvent>

#include <KIcon>
#include <KColorScheme>
#include <KConfigDialog>
#include <KDialog>
#include <KIconLoader>
#include <KPluginInfo>
#include <KStandardDirs>
#include <KService>
#include <KServiceTypeTrader>
#include <KWindowSystem>

#include <Solid/PowerManagement>

#include "plasma/configxml.h"
#include "plasma/containment.h"
#include "plasma/corona.h"
#include "plasma/dataenginemanager.h"
#include "plasma/package.h"
#include "plasma/packages_p.h"
#include "plasma/plasma.h"
#include "plasma/scripting/appletscript.h"
#include "plasma/shadowitem_p.h"
#include "plasma/svg.h"
#include "plasma/svgpanel.h"
#include "plasma/theme.h"
#include "plasma/view.h"

#include "plasma/layouts/boxlayout.h"
#include "plasma/widgets/widget.h"
#include "plasma/widgets/lineedit.h"
#include "plasma/widgets/pushbutton.h"

//#define DYNAMIC_SHADOWS
namespace Plasma
{

class OverlayWidget : public Widget
{
public:
    OverlayWidget(Widget *parent)
        : Widget(parent, parent)
    {
        resize(parent->size());
    }

protected:
    void paintWidget(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget = 0)
    {
        Q_UNUSED(option)
        Q_UNUSED(widget)
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QColor wash = Plasma::Theme::self()->backgroundColor();
        wash.setAlphaF(.6);
        painter->fillPath(parent()->shape(), wash);
        painter->restore();
    }
};

class Applet::Private
{
public:
    Private(KService::Ptr service, int uniqueID)
        : appletId(uniqueID),
          appletDescription(service),
          package(0),
          needsConfigOverlay(0),
          background(0),
          failureText(0),
          script(0),
          configXml(0),
          shadow(0),
          cachedBackground(0),
          mainConfig(0),
          pendingConstraints(NoConstraint),
          aspectRatioMode(Qt::KeepAspectRatio),
          kioskImmutable(false),
          immutable(false),
          hasConfigurationInterface(false),
          failed(false),
          isContainment(false),
          square(false),
          opacity(1.0)
    {
        if (appletId == 0) {
            appletId = ++s_maxAppletId;
        } else if (appletId > s_maxAppletId) {
            s_maxAppletId = appletId;
        }
    }

    ~Private()
    {
        foreach ( const QString& engine, loadedEngines ) {
            DataEngineManager::self()->unload( engine );
        }
        delete background;
        delete package;
        delete configXml;
        delete shadow;
        delete cachedBackground;
        delete mainConfig;
    }

    void init(Applet* applet)
    {
        // WARNING: do not access config() OR globalConfig() in this method!
        //          that requires a scene, which is not available at this point
        applet->setAcceptsHoverEvents(true);

        if (!appletDescription.isValid()) {
            applet->setFailedToLaunch(true, i18n("Invalid applet description"));
            return;
        }

        QString language = appletDescription.property("X-Plasma-Language").toString();

        // we have a scripted plasmoid
        if (!language.isEmpty()) {
            // find where the Package is
            QString path = KStandardDirs::locate("data",
                                                 "plasma/plasmoids/" + appletDescription.pluginName() +
                                                 "/");

            if (path.isEmpty()) {
                applet->setFailedToLaunch(true, i18n("Could not locate the %1 package required for the %2 widget.",
                                                     appletDescription.pluginName(), appletDescription.name()));
            } else {
                // create the package and see if we have something real
                //kDebug() << "trying for" << path;
                PackageStructure::Ptr structure = Plasma::packageStructure(language, Plasma::AppletComponent);
                structure->setPath(path);
                package = new Package(path, structure);

                if (package->isValid()) {
                    // now we try and set up the script engine.
                    // it will be parented to this applet and so will get
                    // deleted when the applet does

                    script = Plasma::loadScriptEngine(language, applet);
                    if (!script) {
                        delete package;
                        package = 0;
                        applet->setFailedToLaunch(true, i18n("Could not create a %1 ScriptEngine for the %2 widget.",
                                                             language, appletDescription.name()));
                    }
                } else {
                    applet->setFailedToLaunch(true, i18n("Could not open the %1 package required for the %2 widget.",
                                                         appletDescription.pluginName(), appletDescription.name()));
                    delete package;
                    package = 0;
                }

                if (package) {
                    setupScriptSupport(applet);
                }
            }
        }
        applet->setDrawStandardBackground(true);

        connect(Plasma::Theme::self(), SIGNAL(changed()), applet, SLOT(themeChanged()));
    }

    // put all setup routines for script here. at this point we can assume that
    // package exists and that we have a script engin
    void setupScriptSupport(Applet* applet)
    {
        Q_ASSERT(package);
        QString xmlPath = package->filePath("mainconfigxml");
        if (!xmlPath.isEmpty()) {
            QFile file(xmlPath);
            // FIXME: KConfigSkeleton doesn't play well with KConfigGroup =/
            KConfigGroup config = applet->config();
            configXml = new ConfigXml(&config, &file);
        }

        if (!package->filePath("mainconfigui").isEmpty()) {
            applet->setHasConfigurationInterface(true);
        }
    }

    QSizeF contentSize(const Applet* q)
    {
        if (failureText) {
            return failureText->geometry().size();
        }

        return q->contentSize();
    }

    QString instanceName()
    {
        if (!appletDescription.isValid()) {
            return QString();
        }

        return appletDescription.service()->library() + QString::number(appletId);
    }

    void getBorderSize(int& left , int& top, int &right, int& bottom)
    {
        if (background) {
            top = background->marginSize(Plasma::TopMargin);
            left = background->marginSize(Plasma::LeftMargin);
            right = background->marginSize(Plasma::RightMargin);
            bottom = background->marginSize(Plasma::BottomMargin);
        } else {
            top = left = right = bottom = 0;
        }
    }

    void scheduleConstraintsUpdate(Plasma::Constraints c, Applet* applet)
    {
        if (pendingConstraints == NoConstraint) {
            QTimer::singleShot(0, applet, SLOT(flushUpdatedConstraints()));
        }
        pendingConstraints |= c;
    }

    KConfigGroup* mainConfigGroup(const Applet* q)
    {
        if (mainConfig) {
            return mainConfig;
        }

        if (isContainment) {
            const Containment *asContainment = qobject_cast<Containment*>(const_cast<Applet*>(q));
            Q_ASSERT(asContainment);

            KConfigGroup containmentConfig;
            //kDebug() << "got a corona, baby?" << (QObject*)asContainment->corona();
            if (asContainment->corona()) {
                containmentConfig = KConfigGroup(asContainment->corona()->config(), "Containments");
            } else {
                containmentConfig =  KConfigGroup(KGlobal::config(), "Containments");
            }

            mainConfig = new KConfigGroup(&containmentConfig, QString::number(appletId));
        } else {
            KConfigGroup appletConfig;
            if (q->containment()) {
                appletConfig = q->containment()->config();
                appletConfig = KConfigGroup(&appletConfig, "Applets");
            } else {
                kWarning() << "requesting config for" << q->name() << "without a containment!";
                appletConfig = KConfigGroup(KGlobal::config(), "Applets");
            }

            mainConfig = new KConfigGroup(&appletConfig, QString::number(appletId));
        }

        return mainConfig;
    }

    void copyEntries(KConfigGroup *source, KConfigGroup *destination)
    {
        foreach (const QString &group, source->groupList()) {
            KConfigGroup subSource(source, group);
            KConfigGroup subDest(destination, group);
            copyEntries(&subSource, &subDest);
        }

        QMap<QString, QString> entries = source->entryMap();
        QMapIterator<QString, QString> it(entries);
        while (it.hasNext()) {
            it.next();
            destination->writeEntry(it.key(), it.value());
        }
    }

    //TODO: examine the usage of memory here; there's a pretty large
    //      number of members at this point.
    static uint s_maxAppletId;
    static uint s_maxZValue;
    static PackageStructure::Ptr packageStructure;
    uint appletId;
    KPluginInfo appletDescription;
    Package* package;
    OverlayWidget *needsConfigOverlay;
    QList<QObject*> watchedForFocus;
    QList<QGraphicsItem*> watchedForMouseMove;
    QStringList loadedEngines;
    Plasma::SvgPanel *background;
    Plasma::LineEdit *failureText;
    AppletScript *script;
    ConfigXml* configXml;
    ShadowItem* shadow;
    QPixmap* cachedBackground;
    KConfigGroup *mainConfig;
    Plasma::Constraints pendingConstraints;
    Qt::AspectRatioMode aspectRatioMode;
    bool kioskImmutable : 1;
    bool immutable : 1;
    bool hasConfigurationInterface : 1;
    bool failed : 1;
    bool isContainment : 1;
    bool square : 1;
    qreal opacity;
};

uint Applet::Private::s_maxAppletId = 0;
uint Applet::Private::s_maxZValue = 0;
PackageStructure::Ptr Applet::Private::packageStructure(0);

Applet::Applet(QGraphicsItem *parent,
               const QString& serviceID,
               uint appletId)
    :  Widget(parent),
       d(new Private(KService::serviceByStorageId(serviceID), appletId))
{
    // WARNING: do not access config() OR globalConfig() in this method!
    //          that requires a scene, which is not available at this point
    d->init(this);
}

Applet::Applet(QObject* parentObject, const QVariantList& args)
    :  Widget(0,parentObject),
       d(new Private(KService::serviceByStorageId(args.count() > 0 ? args[0].toString() : QString()),
                     args.count() > 1 ? args[1].toInt() : 0))
{
    // WARNING: do not access config() OR globalConfig() in this method!
    //          that requires a scene, which is not available at this point
    d->init(this);
    // the brain damage seen in the initialization list is due to the
    // inflexibility of KService::createInstance
}

Applet::~Applet()
{
    needsFocus(false);
    delete d;
}

PackageStructure::Ptr Applet::packageStructure()
{
    if (!Private::packageStructure) {
        Private::packageStructure = new PlasmoidPackage();
    }

    return Private::packageStructure;
}

void Applet::init()
{
    if (d->script && !d->script->init()) {
        setFailedToLaunch(true, i18n("Script initialization failed"));
    }
}

uint Applet::id() const
{
    return d->appletId;
}

void Applet::save(KConfigGroup* group) const
{
    // we call the dptr member directly for locked since isImmutable()
    // also checks kiosk and parent containers
    group->writeEntry("locked", d->immutable);
    group->writeEntry("plugin", pluginName());
    //FIXME: for containments, we need to have some special values here w/regards to
    //       screen affinity (e.g. "bottom of screen 0")
    //kDebug() << pluginName() << "geometry is" << geometry() << "pos is" << pos() << "bounding rect is" << boundingRect();
    group->writeEntry("geometry", geometry());
    group->writeEntry("zvalue", zValue());
    group->writeEntry("opacity", d->opacity);

    if (transform() == QTransform()) {
        group->deleteEntry("transform");
    } else {
        QList<qreal> m;
        QTransform t = transform();
        m << t.m11() << t.m12() << t.m13() << t.m21() << t.m22() << t.m23() << t.m31() << t.m32() << t.m33();
        group->writeEntry("transform", m);
        //group->writeEntry("transform", transformToString(transform()));
    }

    KConfigGroup appletConfigGroup(group, "Configuration");
    //FIXME: we need a global save state too
    saveState(&appletConfigGroup);
}

void Applet::restore(KConfigGroup *c)
{
    QList<qreal> m = c->readEntry("transform", QList<qreal>());
    if (m.count() == 9) {
        QTransform t(m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        setTransform(t);
    }

    qreal z = c->readEntry("zvalue", -1);

    if (z < 0) {
        z = ++Private::s_maxZValue;
    } else if (z >= Private::s_maxZValue) {
        Private::s_maxZValue = z;
    }

    setZValue(z);

    if (c->readEntry("locked", false)) {
        setImmutable(true);
    }

    setOpacity(c->readEntry("opacity", 1.0));
}

void Applet::saveState(KConfigGroup* group) const
{
    if (group->config()->name() != config().config()->name()) {
        // we're being saved to a different file!
        // let's just copy the current values in our configuration over
        KConfigGroup c = config();
        d->copyEntries(&c, group);
    }

    Q_UNUSED(group)
}

KConfigGroup Applet::config(const QString &group) const
{
    KConfigGroup cg = config();
    return KConfigGroup(&cg, group);
}

KConfigGroup Applet::config() const
{
    if (d->isContainment) {
        return *(d->mainConfigGroup(this));
    }

    return KConfigGroup(d->mainConfigGroup(this), "Configuration");
}

KConfigGroup Applet::globalConfig() const
{
    KConfigGroup globalAppletConfig;
    const Containment *c = isContainment() ? dynamic_cast<const Containment*>(this) : containment();
    QString group = isContainment() ? "ContainmentGlobals" : "AppletGlobals";

    if (c && c->corona()) {
        KSharedConfig::Ptr coronaConfig = c->corona()->config();
        globalAppletConfig = KConfigGroup(coronaConfig, group);
    } else {
        globalAppletConfig = KConfigGroup(KGlobal::config(), group);
    }

    return KConfigGroup(&globalAppletConfig, globalName());
}

void Applet::destroy()
{
    //kDebug() << "???????????????? DESTROYING APPLET" << name() << " ???????????????????????????";
    if (d->configXml) {
        d->configXml->setDefaults();
    }

    resetConfigurationObject();
    deleteLater();
}

void Applet::resetConfigurationObject()
{
    d->mainConfigGroup(this)->deleteGroup();
    delete d->mainConfig;
    d->mainConfig = 0;
}

ConfigXml* Applet::configXml() const
{
    return d->configXml;
}

DataEngine* Applet::dataEngine(const QString& name) const
{
    int index = d->loadedEngines.indexOf(name);
    if (index != -1) {
        return DataEngineManager::self()->get(name);
    }

    DataEngine* engine = DataEngineManager::self()->load(name);
    if (engine->isValid()) {
        d->loadedEngines.append(name);
    }

    return engine;
}

const Package* Applet::package() const
{
    return d->package;
}

void Applet::updateConstraints(Plasma::Constraints constraints)
{
    d->scheduleConstraintsUpdate(constraints, this);
}

void Applet::constraintsUpdated(Plasma::Constraints constraints)
{
    //NOTE: do NOT put any code in here that reacts to constraints updates
    //      as it will not get called for any applet that reimplements constraintsUpdated
    //      without calling the Applet:: version as well, which it shouldn't need to.
    //      INSTEAD put such code into flushUpdatedConstraints
    Q_UNUSED(constraints)
    //kDebug() << constraints << "constraints are FormFactor: " << formFactor() << ", Location: " << location();
    if (d->script) {
        d->script->constraintsUpdated(constraints);
    }
}

QString Applet::name() const
{
    if (!d->appletDescription.isValid()) {
        return i18n("Unknown Applet");
    }

    return d->appletDescription.name();
}

QString Applet::icon() const
{
    if (!d->appletDescription.isValid()) {
        return QString();
    }

    return d->appletDescription.icon();
}

QString Applet::pluginName() const
{
    if (!d->appletDescription.isValid()) {
        return QString();
    }

    return d->appletDescription.pluginName();
}

bool Applet::shouldConserveResources() const
{
    return Solid::PowerManagement::appShouldConserveResources();
}

QString Applet::category() const
{
    if (!d->appletDescription.isValid()) {
        return i18n("Miscellaneous");
    }

    return d->appletDescription.category();
}

QString Applet::category(const KPluginInfo& applet)
{
    return applet.property("X-KDE-PluginInfo-Category").toString();
}

QString Applet::category(const QString& appletName)
{
    if (appletName.isEmpty()) {
        return QString();
    }

    QString constraint = QString("[X-KDE-PluginInfo-Name] == '%1'").arg(appletName);
    KService::List offers = KServiceTypeTrader::self()->query("Plasma/Applet", constraint);

    if (offers.isEmpty()) {
        return QString();
    }

    return offers.first()->property("X-KDE-PluginInfo-Category").toString();
}

bool Applet::isImmutable() const
{
    return  d->immutable || d->kioskImmutable ||
            (containment() && containment()->isImmutable()) ||
            (dynamic_cast<Corona*>(scene()) && static_cast<Corona*>(scene())->isImmutable());
}

bool Applet::isKioskImmutable() const
{
    Corona *c = dynamic_cast<Corona*>(scene());
    return d->kioskImmutable || (c && c->isKioskImmutable());
}

void Applet::setImmutable(bool immutable)
{
    if (d->immutable == immutable ||
        (immutable && d->kioskImmutable)) {
        return;
    }

    d->immutable = immutable;
    updateConstraints(ImmutableConstraint);
}

bool Applet::drawStandardBackground() const
{
    return d->background != 0;
}

void Applet::setDrawStandardBackground(bool drawBackground)
{
    if (drawBackground) {
        if (!d->background) {
            d->background = new Plasma::SvgPanel("widgets/background");

            int left, top, right, bottom;
            d->getBorderSize(left, top, right, bottom);
            setMargins(0, 0, right, bottom);
            updateGeometry();
            update();
        }
    } else if (d->background) {
        delete d->background;
        d->background = 0;
        setMargins(0, 0, 0, 0);
        updateGeometry();
        update();
    }
}

bool Applet::failedToLaunch() const
{
    return d->failed;
}

QString visibleFailureText(const QString& reason)
{
    QString text;

    if (reason.isEmpty()) {
        text = i18n("This object could not be created.");
    } else {
        text = i18n("This object could not be created for the following reason:<p><b>%1</b></p>", reason);
    }

    return text;
}

void Applet::setFailedToLaunch(bool failed, const QString& reason)
{
    if (d->failed == failed) {
        if (d->failureText) {
            d->failureText->setHtml(visibleFailureText(reason));
            setGeometry(QRectF(geometry().topLeft(), d->failureText->sizeHint()));
        }
        return;
    }

    d->failed = failed;
    prepareGeometryChange();

    d->failureText = 0;
    qDeleteAll(QGraphicsItem::children());
    setLayout(0);

    if (failed) {
        setDrawStandardBackground(true);
        Layout* failureLayout = new BoxLayout(BoxLayout::TopToBottom, this);
        failureLayout->setMargins(0, 0, 0, 0);
        d->failureText = new LineEdit(this);
        d->failureText->setTextInteractionFlags( Qt::TextSelectableByMouse );
        d->failureText->setStyled(false);
        d->failureText->document()->setTextWidth(200);
        d->failureText->setHtml(visibleFailureText(reason));
        //FIXME: this needs to get the colour from the theme's colour scheme
        d->failureText->setDefaultTextColor(KStatefulBrush(KColorScheme::Window,
                                                           KColorScheme::NormalText,
                                                           Theme::self()->colors())
                                                        .brush(QPalette::Normal).color());
        failureLayout->addItem(d->failureText);
        setGeometry(QRectF(geometry().topLeft(), d->failureText->sizeHint()));
    }

    update();
}

bool Applet::needsConfiguring() const
{
    return d->needsConfigOverlay != 0;
}

void Applet::setNeedsConfiguring(bool needsConfig)
{
    if ((d->needsConfigOverlay != 0) == needsConfig) {
        return;
    }

    if (d->needsConfigOverlay) {
        delete d->needsConfigOverlay;
        d->needsConfigOverlay = 0;
        return;
    }

    d->needsConfigOverlay = new OverlayWidget(this);
    d->needsConfigOverlay->resize(contentSize());

    int zValue = 100;
    foreach (QGraphicsItem *child, QGraphicsItem::children()) {
        if (child->zValue() > zValue) {
            zValue = child->zValue() + 1;
        }
    }
    d->needsConfigOverlay->setZValue(zValue);

    qDeleteAll(d->needsConfigOverlay->QGraphicsItem::children());
    PushButton* button = new PushButton(d->needsConfigOverlay);
    button->setText(i18n("Configure..."));
    connect(button, SIGNAL(clicked()), this, SLOT(showConfigurationInterface()));
    QSizeF s = button->sizeHint();
    button->resize(s);
    button->setPos(d->needsConfigOverlay->boundingRect().width() / 2 - s.width() / 2,
                   d->needsConfigOverlay->boundingRect().height() / 2 - s.height() / 2);
    button->show();
    d->needsConfigOverlay->show();
}

void Applet::checkImmutability()
{
    d->kioskImmutable = globalConfig().isImmutable() || config().isImmutable() ||
                        (containment() && containment()->isKioskImmutable()) ||
                        (dynamic_cast<Corona*>(scene()) && static_cast<Corona*>(scene())->isKioskImmutable());

    if (d->kioskImmutable) {
        updateConstraints(ImmutableConstraint);
    }
}

void Applet::flushUpdatedConstraints()
{
    if (d->pendingConstraints == NoConstraint) {
        return;
    }

    //kDebug() << "fushing constraints: " << d->pendingConstraints << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!";
    Plasma::Constraints c = d->pendingConstraints;
    d->pendingConstraints = NoConstraint;

    if (c & Plasma::SizeConstraint && d->needsConfigOverlay) {
        d->needsConfigOverlay->setGeometry(QRectF(QPointF(0, 0), contentSize()));
        // FIXME:: rather horrible hack to work around the fact we don't have spacers
        //         for layouts, and with WoC coming i'd rather not expend effort there
        QGraphicsItem * button = d->needsConfigOverlay->QGraphicsItem::children().first();
        if (button) {
            QSizeF s = button->boundingRect().size();
            button->setPos(d->needsConfigOverlay->boundingRect().width() / 2 - s.width() / 2,
                           d->needsConfigOverlay->boundingRect().height() / 2 - s.height() / 2);
        }
    }

    if (c & Plasma::FormFactorConstraint) {
        FormFactor f = formFactor();
        setShadowShown(f == Planar);
        setDrawStandardBackground(!isContainment() && f != Vertical && f != Horizontal);

        /**
         FIXME: what follows was an attempt to constrain the size of applets. it is, however,
                broken for the following reasons:

                * it constrains to the size of an icon, when clearly this is not valid for
                  any non-single-icon applet
                * it is far too pessimistic for horizontal constraints

        QSizeF newMax;
        const QSizeF infSize(std::numeric_limits<qreal>::infinity(),
                std::numeric_limits<qreal>::infinity());
        if (f == Plasma::Vertical && !(expandingDirections() & Qt::Vertical)) {
            newMax = QSizeF(maximumContentSize().width(), IconSize(KIconLoader::Panel));
        } else if (f == Plasma::Horizontal && !(expandingDirections() & Qt::Horizontal)) {
            newMax = QSizeF(IconSize(KIconLoader::Panel), maximumContentSize().height());
        } else if (maximumContentSize() != infSize) {
            newMax = infSize;
        }

        if (newMax.isValid()) {
            setMaximumContentSize(newMax);
            if (newMax.width() < contentSize().width() ||
                newMax.height() < contentSize().height()) {
                updateGeometry();
            }
        }
        */
    }

    Containment* containment = qobject_cast<Plasma::Containment*>(this);
    if (isContainment() && containment) {
        containment->containmentConstraintsUpdated(c);
    }

    constraintsUpdated(c);

    if (layout()) {
        layout()->updateGeometry();
    }
}

void Applet::launchActivated()
{
    if (containment()) {
        containment()->emitLaunchActivated();
    }
}

int Applet::type() const
{
    return Type;
}

QRectF Applet::boundingRect() const
{
    QRectF rect = QRectF(QPointF(0,0), d->contentSize(this));

    int left;
    int right;
    int top;
    int bottom;

    d->getBorderSize(left, top, right, bottom);
    //kDebug() << "Background , Border size" << d->background << left << top << right << bottom;
    return rect.adjusted(-left, -top, right, bottom);
}

QPainterPath Applet::shape() const
{
    if (isContainment()) {
        return Plasma::Widget::shape();
    }

    return Plasma::roundedRectangle(boundingRect().adjusted(-2, -2, 2, 2), 10);
}

QSizeF Applet::sizeHint() const
{
    int left = 0;
    int right = 0;
    int top = 0;
    int bottom = 0;

    d->getBorderSize(left, top, right, bottom);
    QSizeF borderSize = QSizeF(left + right, top + bottom);

    //kDebug() << "Applet content size hint: " << contentSizeHint() << "plus our borders" << left << right << top << bottom;

    return contentSizeHint() + QSizeF(left + right, top + bottom);
}

Qt::Orientations Applet::expandingDirections() const
{
    if (d->square) {
        return 0;
    }

    return Widget::expandingDirections();
}

QList<QAction*> Applet::contextActions()
{
    //kDebug() << "empty context actions";
    return d->script ? d->script->contextActions() : QList<QAction*>();
}

QColor Applet::color() const
{
    // TODO: add more colors for more categories and
    // maybe read from config?
    QString c = category();
    int alpha = 200;
    // Colors taken from Oxygen color palette
    if (c == "Date and Time") {
        return QColor(191, 94, 0, alpha);
    } else if (c == "Environment & Weather") {
        return QColor(191, 0, 0, alpha);
    } else if (c == "Examples") {
        return QColor(204, 0, 154, alpha);
    } else if (c == "File System") {
        return QColor(90, 0, 179, alpha);
    } else if (c == "Graphics") {
        return QColor(0, 0, 255, alpha);
    } else if (c == "Language") {
        return QColor(0, 191, 0, alpha);
    } else if (c == "Mapping") {
        return QColor(191, 245, 0, alpha);
    } else if (c == "Online Services") {
        return QColor(255, 213, 0, alpha);
    } else if (c == "System Information") {
        return QColor(0, 196, 204, alpha);
    } else if (c == "Windows and Tasks") {
        return QColor(255, 126, 0, alpha);
    } else {
        return QColor(136, 136, 136, alpha);
    }
}

void Applet::paintWidget(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    if (d->shadow && d->shadow->shadowedSize() != boundingRect().size()) {
        //kDebug() << "sizes are " << d->shadow->shadowedSize() << boundingRect().size();
        d->shadow->generate();
    }

    painter->save();

    //Set opacity
    if (d->opacity >= 0.1 && d->opacity <= 0.9) {
        painter->setOpacity(d->opacity);
    }

    if (transform().isRotating()) {
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->setRenderHint(QPainter::Antialiasing);
    }

    if (d->background &&
        formFactor() != Plasma::Vertical &&
        formFactor() != Plasma::Horizontal) {
        //kDebug() << "option rect is" << option->rect;
        d->background->paint(painter, option->rect);
    }

    if (!d->failed) {
        if (widget && isContainment()) {
            // note that the widget we get is actually the viewport of the view, not the view itself
            View* v = qobject_cast<Plasma::View*>(widget->parent());
            if (!v || v->drawWallpaper()) {
                Containment::StyleOption coption(*option);

                if (v) {
                    coption.desktop = v->effectiveDesktop();
                    coption.view = v;
                }

                paintInterface(painter, &coption, QRect(QPoint(0,0), d->contentSize(this).toSize()));
            }

            painter->restore();
            return;
        }

        //kDebug() << "paint interface of" << (QObject*) this;
        paintInterface(painter, option, QRect(QPoint(0,0), d->contentSize(this).toSize()));
    }

    painter->restore();
}

void Applet::paintInterface(QPainter *painter, const QStyleOptionGraphicsItem *option,
                            const QRect & contentsRect)
{
    if (d->script) {
        d->script->paintInterface(painter, option, contentsRect);
    } else {
        //kDebug() << "Applet::paintInterface() default impl";
    }
}

FormFactor Applet::formFactor() const
{
    Containment* c = containment();
    return c ? c->formFactor() : Plasma::Planar;
}

Containment* Applet::containment() const
{
/*
 * while this is probably "more correct", much of the code in applet assumes containment
 * returns zero in the case that this is a containment itself.
 * if (isContainment()) {
        return dynamic_cast<Containment*>(const_cast<Applet*>(this));
    }
*/

    QGraphicsItem *parent = parentItem();
    Containment *c = 0;

    while (parent) {
        Containment *possibleC =  dynamic_cast<Containment*>(parent);
        if (possibleC && possibleC->isContainment()) {
            c = possibleC;
            break;
        }
        parent = parent->parentItem();
    }

    return c;
}

Location Applet::location() const
{
    Containment* c = containment();

    if (!c) {
        return Plasma::Desktop;
    }

    return c->location();
}

QRectF Applet::contentRect() const
{
    return QRectF(QPointF(0, 0), contentSize());
}

QSizeF Applet::contentSize() const
{
    int top, left, right, bottom;
    d->getBorderSize(left, top, right, bottom);

    // kDebug() << "Geometry size: " << geometry().size();
    // kDebug() << "Borders: " << left << top << right << bottom;

    return (geometry().size() - QSizeF(left + right, top + bottom)).expandedTo(QSizeF(0, 0));
}

void Applet::setContentSize(const QSizeF &size)
{
    int top, left, right, bottom;
    d->getBorderSize(left, top, right, bottom);

    resize(size + QSizeF(left + right, top + bottom));
}

void Applet::setContentSize(int width, int height)
{
    setContentSize(QSizeF(width, height));
}

QSizeF Applet::contentSizeHint() const
{
    static bool checkingScript = false;

    if (!checkingScript && d->script) {
        checkingScript = true;
        return d->script->contentSizeHint();
    }

    checkingScript = false;
    QSizeF size;
    if (layout()) {
        size = layout()->sizeHint();
    } else {
        size = contentSize();
    }

    QSizeF max = maximumContentSize();
    size = size.boundedTo(max);
    if (d->square) {
        //kDebug() << "SizeHintIn: " << (QObject*)this << size;
        switch (formFactor()) {
            case Plasma::Vertical:
                if (size.width() > max.height()) {
                    size.setWidth(max.height());
                }

                size.setHeight(size.width());
            case Plasma::Horizontal:
            case Plasma::Planar:
            case Plasma::MediaCenter:
                if (size.height() > max.width()) {
                    size.setHeight(max.width());
                }

                size.setWidth(size.height());
            default:
                break;
        }

        //kDebug() << "SizeHintOut: " << size;
        return size;
    }

    return size;
}

void Applet::setMinimumContentSize(const QSizeF &minSize)
{
    int top, left, right, bottom;
    d->getBorderSize(left, top, right, bottom);

    setMinimumSize(minSize + QSizeF(left + right, top + bottom));
}

void Applet::setMinimumContentSize(int minWidth, int minHeight)
{
    setMinimumContentSize(QSizeF(minWidth, minHeight));
}

QSizeF Applet::minimumContentSize() const
{
    int top, left, right, bottom;
    d->getBorderSize(left, top, right, bottom);

    return minimumSize() - QSizeF(left + right, top + bottom);
}

void Applet::setMaximumContentSize(const QSizeF &maxSize)
{
    int top, left, right, bottom;
    d->getBorderSize(left, top, right, bottom);

    setMaximumSize(maxSize + QSizeF(left + right, top + bottom));
}

void Applet::setMaximumContentSize(int maxWidth, int maxHeight)
{
    setMaximumContentSize(QSizeF(maxWidth, maxHeight));
}

QSizeF Applet::maximumContentSize() const
{
    int top, left, right, bottom;
    d->getBorderSize(left, top, right, bottom);

    return maximumSize() - QSizeF(left + right, top + bottom);
}

Qt::AspectRatioMode Applet::aspectRatioMode() const
{
    return d->aspectRatioMode;
}

void Applet::setAspectRatioMode(Qt::AspectRatioMode mode)
{
    d->aspectRatioMode = mode;
}

bool Applet::remainSquare() const
{
    return d->square;
}

void Applet::setRemainSquare(bool square)
{
    d->square = square;
}

QString Applet::globalName() const
{
    if (!d->appletDescription.isValid()) {
        return QString();
    }

    return d->appletDescription.service()->library();
}

QString Applet::instanceName() const
{
    return d->instanceName();
}

void Applet::watchForFocus(QObject *widget, bool watch)
{
    if (!widget) {
        return;
    }

    int index = d->watchedForFocus.indexOf(widget);
    if (watch) {
        if (index == -1) {
            d->watchedForFocus.append(widget);
            widget->installEventFilter(this);
        }
    } else if (index != -1) {
        d->watchedForFocus.removeAt(index);
        widget->removeEventFilter(this);
    }
}

void Applet::watchForMouseMove( QGraphicsItem * watched, bool watch )
{
    if (!watched) {
        return;
    }

    int index = d->watchedForMouseMove.indexOf(watched);
    if (watch) {
        if (index == -1) {
            d->watchedForMouseMove.append(watched);
            watched->installSceneEventFilter(this);
        }
    } else if (index != -1) {
        d->watchedForMouseMove.removeAt(index);
        watched->removeSceneEventFilter(this);
    }
}

void Applet::needsFocus(bool focus)
{
    if (focus == QGraphicsItem::hasFocus()) {
        return;
    }

    emit requestFocus(focus);
}

bool Applet::hasConfigurationInterface()
{
    return d->hasConfigurationInterface;
}

void Applet::setHasConfigurationInterface(bool hasInterface)
{
    d->hasConfigurationInterface = hasInterface;
}

bool Applet::eventFilter( QObject *o, QEvent * e )
{
    if ( !d->watchedForFocus.contains( o ) )
    {
        if ( e->type() == QEvent::MouseButtonRelease ||
             e->type() == QEvent::FocusIn ) {
            needsFocus( true );
        } else if ( e->type() == QEvent::FocusOut ) {
            needsFocus( false );
        }
    }

    return QObject::eventFilter(o, e);
}

bool Applet::sceneEventFilter( QGraphicsItem * watched, QEvent * event )
{
    switch (event->type()) {
        case QEvent::GraphicsSceneMouseMove: {
            if (d->watchedForMouseMove.contains( watched )) {
                mouseMoveEvent(static_cast<QGraphicsSceneMouseEvent*>(event));
                return true;
            }
            break;
        }

        default:
            break;
    }

    return QGraphicsItem::sceneEventFilter(watched, event);
}

void Applet::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (!isImmutable() && formFactor() == Plasma::Planar) {
        QGraphicsItem *parent = parentItem();
        Plasma::Applet *applet = qgraphicsitem_cast<Plasma::Applet*>(parent);

        if (applet && applet->isContainment()) {
            // our direct parent is a containment. just move ourselves.
            QPointF curPos = event->pos();
            QPointF lastPos = event->lastPos();
            QPointF delta = curPos-lastPos;

            moveBy(delta.x(),delta.y());
        } else if (parent) {
            //don't move the icon as well because our parent (usually an appletHandle) will do it for us
            //parent->moveBy(delta.x(),delta.y());
            QPointF curPos = parent->transform().map(event->pos());
            QPointF lastPos = parent->transform().map(event->lastPos());
            QPointF delta = curPos-lastPos;

            parent->setPos(parent->pos() + delta);
        }

    }
}


void Applet::showConfigurationInterface()
{
    if (!hasConfigurationInterface()) {
        return;
    }

    const QString dialogId = QString("%1settings%2").arg(id()).arg(name());
    KConfigDialog * dlg = KConfigDialog::exists(dialogId);

    if (dlg) {
        KWindowSystem::setOnDesktop(dlg->winId(), KWindowSystem::currentDesktop());
        dlg->show();
        KWindowSystem::activateWindow(dlg->winId());
        return;
    }

    const QString windowTitle = i18nc("@title:window", "%1 Settings", name());
    if (d->package && d->configXml) {
        QString uiFile = d->package->filePath("mainconfigui");
        if (uiFile.isEmpty()) {
            return;
        }

        KConfigDialog *dialog = new KConfigDialog(0, dialogId, d->configXml);
        dialog->setWindowTitle(windowTitle);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);

        QUiLoader loader;
        QFile f(uiFile);
        if (!f.open(QIODevice::ReadOnly)) {
            delete dialog;
            return;
        }

        QWidget *w = loader.load(&f);
        f.close();

        dialog->addPage(w, i18n("Settings"), icon(), i18n("%1 Settings", name()));
        dialog->show();
    } else if (d->script) {
        d->script->showConfigurationInterface();
    } else {
        KConfigSkeleton *nullManager = new KConfigSkeleton(0);
        KConfigDialog *dialog = new KConfigDialog(0, dialogId, nullManager);
        dialog->setFaceType(KPageDialog::Auto);
        dialog->setWindowTitle(windowTitle);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
        createConfigurationInterface(dialog);
        //TODO: would be nice to not show dialog if there are no pages added?
        connect(dialog, SIGNAL(finished()), nullManager, SLOT(deleteLater()));
        dialog->show();
        dialog->enableButtonApply(true);
    }
}

void Applet::createConfigurationInterface(KConfigDialog *parent)
{
    Q_UNUSED(parent)
    // virtual method reimplemented by subclasses.
    // do not put anything here ...
}

KPluginInfo::List Applet::knownApplets(const QString &category,
                                       const QString &parentApp)
{
    QString constraint;

    if (parentApp.isEmpty()) {
        constraint.append("not exist [X-KDE-ParentApp]");
    } else {
        constraint.append("[X-KDE-ParentApp] == '").append(parentApp).append("'");
    }

    if (!category.isEmpty()) {
        if (!constraint.isEmpty()) {
            constraint.append(" and ");
        }

        constraint.append("[X-KDE-PluginInfo-Category] == '").append(category).append("'");
        if (category == "Miscellaneous") {
            constraint.append(" or (not exist [X-KDE-PluginInfo-Category] or [X-KDE-PluginInfo-Category] == '')");
        }
    }

    KService::List offers = KServiceTypeTrader::self()->query("Plasma/Applet", constraint);
    //kDebug() << "Applet::knownApplets constraint was '" << constraint << "' which got us " << offers.count() << " matches";
    return KPluginInfo::fromServices(offers);
}

KPluginInfo::List Applet::knownAppletsForMimetype(const QString &mimetype)
{
    QString constraint = QString("'%1' in MimeTypes").arg(mimetype);
    //kDebug() << "knownAppletsForMimetype with" << mimetype << constraint;
    KService::List offers = KServiceTypeTrader::self()->query("Plasma/Applet", constraint);
    return KPluginInfo::fromServices(offers);
}

QStringList Applet::knownCategories(const QString &parentApp, bool visibleOnly)
{
    QString constraint = "exist [X-KDE-PluginInfo-Category]";

    if (parentApp.isEmpty()) {
        constraint.append(" and not exist [X-KDE-ParentApp]");
    } else {
        constraint.append(" and [X-KDE-ParentApp] == '").append(parentApp).append("'");
    }

    KService::List offers = KServiceTypeTrader::self()->query("Plasma/Applet", constraint);
    QStringList categories;
    foreach (const KService::Ptr applet, offers) {
        QString appletCategory = applet->property("X-KDE-PluginInfo-Category").toString();
        if (visibleOnly && applet->noDisplay()) {
            // we don't want to show the hidden category
            continue;
        }

        //kDebug() << "   and we have " << appletCategory;
        if (appletCategory.isEmpty()) {
            if (!categories.contains(i18n("Miscellaneous"))) {
                categories << i18n("Miscellaneous");
            }
        } else  if (!categories.contains(appletCategory)) {
            categories << appletCategory;
        }
    }

    categories.sort();
    return categories;
}

Applet* Applet::load(const QString& appletName, uint appletId, const QVariantList& args)
{
    if (appletName.isEmpty()) {
        return 0;
    }

    QString constraint = QString("[X-KDE-PluginInfo-Name] == '%1'").arg(appletName);
    KService::List offers = KServiceTypeTrader::self()->query("Plasma/Applet", constraint);

    bool isContainment = false;
    if (offers.isEmpty()) {
        //TODO: what would be -really- cool is offer to try and download the applet
        //      from the network at this point
        offers = KServiceTypeTrader::self()->query("Plasma/Containment", constraint);
        isContainment = true;
        if (offers.isEmpty()) {
            kDebug() << "offers is empty for " << appletName;
            return 0;
        }
    } /* else if (offers.count() > 1) {
        kDebug() << "hey! we got more than one! let's blindly take the first one";
    } */

    KService::Ptr offer = offers.first();

    if (appletId == 0) {
        appletId = ++Private::s_maxAppletId;
    }

    if (!offer->property("X-Plasma-Language").toString().isEmpty()) {
        kDebug() << "we have a script in the language of" << offer->property("X-Plasma-Language").toString();
        if (isContainment) {
            return new Containment(0, offer->storageId(), appletId);
        }
        return new Applet(0, offer->storageId(), appletId);
    }

    QVariantList allArgs;
    allArgs << offer->storageId() << appletId << args;
    QString error;
    Applet* applet = offer->createInstance<Plasma::Applet>(0, allArgs, &error);

    if (!applet) {
        kDebug() << "Couldn't load applet \"" << appletName << "\"! reason given: " << error;
    }

    return applet;
}

Applet* Applet::load(const KPluginInfo& info, uint appletId, const QVariantList& args)
{
    if (!info.isValid()) {
        return 0;
    }

    return load(info.pluginName(), appletId, args);
}

void Applet::setShadowShown(bool shown)
{
    //There are various problems with shadows right now:
    //
    //1) shadows can be seen through translucent areas, which is probably technically correct ubt
    //looks odd
    //2) the shape of the item odesn't conform to the shape of the standard background, e.g. with
    //rounded corners
#ifdef DYNAMIC_SHADOWS
    if (shown) {
        if (d->shadow) {
            d->shadow->setVisible(true);
        } else {
            d->shadow = new ShadowItem(this);
            if (scene()) {
                scene()->addItem(d->shadow);
                d->shadow->show();
            }
        }
    } else {
        delete d->shadow;
        d->shadow = 0;
    }
#else
    Q_UNUSED(shown);
#endif
}

bool Applet::isShadowShown() const
{
    return d->shadow && d->shadow->isVisible();
}

QPointF Applet::topLeft() const
{
    return boundingRect().topLeft();
}

QVariant Applet::itemChange(GraphicsItemChange change, const QVariant &value)
{
    switch (change) {
    case ItemPositionChange:
        if (d->shadow) {
            d->shadow->adjustPosition();
        }
        break;
    case ItemSceneHasChanged: {
        QGraphicsScene *newScene = qvariant_cast<QGraphicsScene*>(value);
        if (newScene) {
            checkImmutability();
        }

        if (d->shadow) {
            if (d->shadow->scene()) {
                d->shadow->scene()->removeItem(d->shadow);
            }

            if (newScene) {
                newScene->addItem(d->shadow);
                d->shadow->generate();
                d->shadow->adjustPosition();
                d->shadow->show();
            }
        }
    }
        break;
    case ItemVisibleChange:
        if (d->shadow) {
            d->shadow->setVisible(isVisible());
        }
        break;
    default:
        break;
    };

    return Widget::itemChange(change, value);
}

void Applet::setGeometry(const QRectF& geometry)
{
    QSizeF s = size();
    QPointF p = pos();

    Widget::setGeometry(geometry);
    //kDebug() << s << size();
    if (s != size()) {
        if (d->background) {
            //kDebug() << "setting background to" << size();
            d->background->resize(size());
        }

        updateConstraints(Plasma::SizeConstraint);
        emit geometryChanged();
    } else if (p != pos()) {
        emit geometryChanged();
    }
}

void Applet::raise()
{
    setZValue(++Private::s_maxZValue);
}

void Applet::setIsContainment(bool isContainment)
{
    d->isContainment = isContainment;
}

bool Applet::isContainment() const
{
    return d->isContainment;
}

void Applet::themeChanged()
{
    update();
}

void Applet::setOpacity( qreal opacity )
{
    d->opacity = opacity;
}

qreal Applet::opacity() const
{
    return d->opacity;
}

} // Plasma namespace

#include "applet.moc"

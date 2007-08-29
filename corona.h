/*
 *   Copyright 2007 Matt Broadstone <mbroadst@gmail.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License version 2 as
 *   published by the Free Software Foundation
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

#ifndef CORONA_H
#define CORONA_H

#include <QtGui/QGraphicsScene>

#include <plasma/applet.h>
#include <plasma/plasma.h>
#include <plasma/plasma_export.h>

namespace Plasma
{


/**
 * @short A QGraphicsScene for Plasma::Applets
 */
class PLASMA_EXPORT Corona : public QGraphicsScene
{
    Q_OBJECT

//typedef QHash<QString, QList<Plasma::Applet*> > layouts;

public:
    explicit Corona(QObject * parent = 0);
    explicit Corona(const QRectF & sceneRect, QObject * parent = 0);
    explicit Corona(qreal x, qreal y, qreal width, qreal height, QObject * parent = 0);
    ~Corona();

    /**
     * The applets and Corona surface are changeable or not
     **/
    bool isImmutable() const;

    /**
     * The location of the Corona. @see Plasma::Location
     */
    Plasma::Location location() const;

    /**
     * The current form factor for this Corona. @see Plasma::FormFactor
     **/
    Plasma::FormFactor formFactor() const;

    /**
     * A rect containing the maximum size a plasmoid on this corona should
     * consider being.
     **/
    QRectF maxSizeHint() const;

    /**
     *  Sets the mimetype of Drag/Drop items. Default is
     *  text/x-plasmoidservicename
     */
    void setAppletMimeType(const QString &mimetype);

    /**
     * The current mime type of Drag/Drop items.
     */
    QString appletMimeType();

    /**
     * Save applets to a config file
     *
     * @param config the name of the config file to save to
     */
    void saveApplets(const QString &config) const;

    /**
     * Load applet layout from a config file
     *
     * @param config the name of the config file to load from
     */
    void loadApplets(const QString &config);

    /**
     * Clear the Corona from all applets.
     */
    void clearApplets();
    
public Q_SLOTS:
    /**
     * Load applets from the default config file
     */
    void loadApplets();

    /**
     * Save applets to the default config file
     */
    void saveApplets() const;

    /**
     * Informs the Corona as to what position it is in. This is informational
     * only, as the Corona doesn't change it's actual location. This is,
     * however, passed on to Applets that may be managed by this Corona.
     *
     * @param location the new location of this Corona
     */
    void setLocation(Plasma::Location location);

    /**
     * Sets the form factor for this Corona. This may cause changes in both
     * the arrangement of Applets as well as the display choices of individual
     * Applets.
     */
    void setFormFactor(Plasma::FormFactor formFactor);

    /**
     * Adds an applet to the Corona
     *
     * @param name the plugin name for the applet, as given by 
     *        KPluginInfo::pluginName()
     * @param args argument list to pass to the plasmoid
     * @param id to assign to this applet, or 0 to auto-assign it a new id
     * @param geometry where to place the applet, or to auto-place it if an invalid
     *                 is provided
     *
     * @return a pointer to the applet on success, or 0 on failure
     */
    Applet* addApplet(const QString& name, const QVariantList& args = QVariantList(),
                      uint id = 0, const QRectF &geometry = QRectF(-1, -1, -1, -1));

    /**
     * Adds a SuperKaramba theme to the scene
     *
     * @param path the path to the theme file
     */
    void addKaramba(const KUrl& path);

    /**
     * Sets if the applets are Immutable
     */
    void setImmutable(bool immutable_);

protected:
    void dragEnterEvent(QGraphicsSceneDragDropEvent* event);
    void dragLeaveEvent(QGraphicsSceneDragDropEvent* event);
    void dragMoveEvent(QGraphicsSceneDragDropEvent* event);
    void dropEvent(QGraphicsSceneDragDropEvent* event);
//     void contextMenuEvent(QGraphicsSceneContextMenuEvent *contextMenuEvent);

protected Q_SLOTS:
    void appletDestroyed(QObject*);

private:
    class Private;
    Private * const d;
};

} // namespace Plasma

#endif



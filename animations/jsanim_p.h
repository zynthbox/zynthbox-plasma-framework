/***********************************************************************/
/* jsanim_p.h 					       */
/* 								       */
/* Copyright (C)  2010  Adenilson Cavalcanti <cavalcantii@gmail.com>   */
/* 								       */
/* This library is free software; you can redistribute it and/or       */
/* modify it under the terms of the GNU Lesser General Public	       */
/* License as published by the Free Software Foundation; either	       */
/* version 2.1 of the License, or (at your option) any later version.  */
/*   								       */
/* This library is distributed in the hope that it will be useful,     */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of      */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU   */
/* Lesser General Public License for more details.		       */
/*  								       */
/* You should have received a copy of the GNU Lesser General Public    */
/* License along with this library; if not, write to the Free Software */
/* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA       */
/* 02110-1301  USA						       */
/***********************************************************************/

#ifndef PLASMA_ANIMATIONS_JS_P_H
#define PLASMA_ANIMATIONS_JS_P_H

#include <plasma/animations/animation.h>
#include <plasma/plasma_export.h>

class QString;
class QScriptEngine;
class QScriptValue;

namespace Plasma
{

class JavascriptAnimation: public Animation
{
    Q_OBJECT

public:

    explicit JavascriptAnimation(const QString &name, QObject *parent = 0);

    ~JavascriptAnimation();

protected:
    void updateState(QAbstractAnimation::State newState, QAbstractAnimation::State oldState);
    void updateCurrentTime(int currentTime);

private:
    int m_fps;
    QString m_name;
    QScriptEngine *engine;
    QScriptValue *m_instance;
    QScriptValue *m_method;
};

}

#endif


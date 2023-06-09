/********************************************************************
Copyright 2021  luochaojiang <luochaojiang@uniontech.com>

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) version 3, or any
later version accepted by the membership of KDE e.V. (or its
successor approved by the membership of KDE e.V.), which shall
act as a proxy defined in Section 6 of version 3 of the license.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/
#ifndef WAYLAND_SERVER_DDE_KEYBOARD_INTERFACE_H
#define WAYLAND_SERVER_DDE_KEYBOARD_INTERFACE_H

#include <QObject>
#include <QPoint>
#include <QSize>
#include <QVector>

#include <KWayland/Server/kwaylandserver_export.h>
#include "global.h"
#include "resource.h"

struct wl_resource;

namespace KWayland
{
namespace Server
{

class Display;
class DDESeatInterface;

/**
 * @brief Resource for the dde_keyboard interface.
 *
 * DDEKeyboardInterface gets created by DDESeatInterface.
 *
 * @since 5.4
 **/
class KWAYLANDSERVER_EXPORT DDEKeyboardInterface : public Resource
{
    Q_OBJECT
public:
    explicit DDEKeyboardInterface(DDESeatInterface *ddeSeat, wl_resource *parentResource);
    virtual ~DDEKeyboardInterface();

    /**
     * @returns The DDESeatInterface which created this DDEPointerInterface.
     **/
    DDESeatInterface *ddeSeat() const;

    void setKeymap(int fd, quint32 size);
    void updateModifiers(quint32 depressed, quint32 latched, quint32 locked, quint32 group, quint32 serial);
    void keyPressed(quint32 key, quint32 serial);
    void keyReleased(quint32 key, quint32 serial);
    void repeatInfo(qint32 charactersPerSecond, qint32 delay);

private:
    class Private;
    Private *d_func() const;
};

}
}

#endif
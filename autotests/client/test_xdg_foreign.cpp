/*
    SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Marco Martin <mart@kde.org>

    SPDX-License-Identifier: LGPL-2.1-only OR LGPL-3.0-only OR LicenseRef-KDE-Accepted-LGPL
*/
// Qt
#include <QSignalSpy>
#include <QTest>
// KWin
#include "../../src/client/compositor.h"
#include "../../src/client/connection_thread.h"
#include "../../src/client/event_queue.h"
#include "../../src/client/region.h"
#include "../../src/client/registry.h"
#include "../../src/client/surface.h"
#include "../../src/client/xdgforeign.h"
#include "../../src/server/compositor_interface.h"
#include "../../src/server/display.h"
#include "../../src/server/surface_interface.h"
#include "../../src/server/xdgforeign_interface.h"

using namespace KWayland::Client;

class TestForeign : public QObject
{
    Q_OBJECT
public:
    explicit TestForeign(QObject *parent = nullptr);
private Q_SLOTS:
    void init();
    void cleanup();

    void testExport();
    void testDeleteImported();
    void testDeleteChildSurface();
    void testDeleteParentSurface();
    void testDeleteExported();
    void testExportTwoTimes();
    void testImportTwoTimes();

private:
    void doExport();

    KWayland::Server::Display *m_display;
    KWayland::Server::CompositorInterface *m_compositorInterface;
    KWayland::Server::XdgForeignInterface *m_foreignInterface;
    KWayland::Client::ConnectionThread *m_connection;
    KWayland::Client::Compositor *m_compositor;
    KWayland::Client::EventQueue *m_queue;
    KWayland::Client::XdgExporter *m_exporter;
    KWayland::Client::XdgImporter *m_importer;

    QPointer<KWayland::Client::Surface> m_exportedSurface;
    QPointer<KWayland::Server::SurfaceInterface> m_exportedSurfaceInterface;

    QPointer<KWayland::Client::XdgExported> m_exported;
    QPointer<KWayland::Client::XdgImported> m_imported;

    QPointer<KWayland::Client::Surface> m_childSurface;
    QPointer<KWayland::Server::SurfaceInterface> m_childSurfaceInterface;

    QThread *m_thread;
};

static const QString s_socketName = QStringLiteral("kwayland-test-xdg-foreign-0");

TestForeign::TestForeign(QObject *parent)
    : QObject(parent)
    , m_display(nullptr)
    , m_compositorInterface(nullptr)
    , m_connection(nullptr)
    , m_compositor(nullptr)
    , m_queue(nullptr)
    , m_exporter(nullptr)
    , m_importer(nullptr)
    , m_thread(nullptr)
{
}

void TestForeign::init()
{
    using namespace KWayland::Server;
    delete m_display;
    m_display = new Display(this);
    m_display->setSocketName(s_socketName);
    m_display->start();
    QVERIFY(m_display->isRunning());

    qRegisterMetaType<KWayland::Server::SurfaceInterface *>("KWayland::Server::SurfaceInterface");

    // setup connection
    m_connection = new KWayland::Client::ConnectionThread;
    QSignalSpy connectedSpy(m_connection, &ConnectionThread::connected);
    QVERIFY(connectedSpy.isValid());
    m_connection->setSocketName(s_socketName);

    m_thread = new QThread(this);
    m_connection->moveToThread(m_thread);
    m_thread->start();

    m_connection->initConnection();
    QVERIFY(connectedSpy.wait());

    m_queue = new KWayland::Client::EventQueue(this);
    QVERIFY(!m_queue->isValid());
    m_queue->setup(m_connection);
    QVERIFY(m_queue->isValid());

    Registry registry;
    QSignalSpy compositorSpy(&registry, &Registry::compositorAnnounced);
    QVERIFY(compositorSpy.isValid());

    QSignalSpy exporterSpy(&registry, &Registry::exporterUnstableV2Announced);
    QVERIFY(exporterSpy.isValid());

    QSignalSpy importerSpy(&registry, &Registry::importerUnstableV2Announced);
    QVERIFY(importerSpy.isValid());

    QVERIFY(!registry.eventQueue());
    registry.setEventQueue(m_queue);
    QCOMPARE(registry.eventQueue(), m_queue);
    registry.create(m_connection->display());
    QVERIFY(registry.isValid());
    registry.setup();

    m_compositorInterface = m_display->createCompositor(m_display);
    m_compositorInterface->create();
    QVERIFY(m_compositorInterface->isValid());

    QVERIFY(compositorSpy.wait());
    const auto name = compositorSpy.first().first().value<quint32>();
    const auto version = compositorSpy.first().last().value<quint32>();
    m_compositor = registry.createCompositor(name, version, this);

    m_foreignInterface = m_display->createXdgForeignInterface(m_display);
    m_foreignInterface->create();
    QVERIFY(m_foreignInterface->isValid());

    QVERIFY(exporterSpy.wait());
    // Both importer and exporter should have been triggered by now
    QCOMPARE(exporterSpy.count(), 1);
    QCOMPARE(importerSpy.count(), 1);

    m_exporter = registry.createXdgExporter(exporterSpy.first().first().value<quint32>(), exporterSpy.first().last().value<quint32>(), this);
    m_importer = registry.createXdgImporter(importerSpy.first().first().value<quint32>(), importerSpy.first().last().value<quint32>(), this);
}

void TestForeign::cleanup()
{
#define CLEANUP(variable)                                                                                                                                      \
    if (variable) {                                                                                                                                            \
        delete variable;                                                                                                                                       \
        variable = nullptr;                                                                                                                                    \
    }

    CLEANUP(m_exportedSurfaceInterface)
    CLEANUP(m_childSurfaceInterface)

    CLEANUP(m_compositor)
    CLEANUP(m_exporter)
    CLEANUP(m_importer)
    CLEANUP(m_queue)
    if (m_connection) {
        m_connection->deleteLater();
        m_connection = nullptr;
    }
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        delete m_thread;
        m_thread = nullptr;
    }
    CLEANUP(m_compositorInterface)
    CLEANUP(m_foreignInterface)

    // internally there are some deleteLaters on exported interfaces
    // we want them processed before we delete the connection
    if (m_display) {
        QSignalSpy destroyedSpy(m_display, &QObject::destroyed);
        m_display->deleteLater();
        m_display = nullptr;
        destroyedSpy.wait();
    }

#undef CLEANUP
}

void TestForeign::doExport()
{
    QSignalSpy serverSurfaceCreated(m_compositorInterface, &KWayland::Server::CompositorInterface::surfaceCreated);
    QVERIFY(serverSurfaceCreated.isValid());

    m_exportedSurface = m_compositor->createSurface();
    QVERIFY(serverSurfaceCreated.wait());

    m_exportedSurfaceInterface = serverSurfaceCreated.first().first().value<KWayland::Server::SurfaceInterface *>();

    // Export a window
    m_exported = m_exporter->exportTopLevel(m_exportedSurface, m_connection);
    QVERIFY(m_exported->handle().isEmpty());
    QSignalSpy doneSpy(m_exported.data(), &XdgExported::done);
    QVERIFY(doneSpy.wait());
    QVERIFY(!m_exported->handle().isEmpty());

    QSignalSpy transientSpy(m_foreignInterface, &KWayland::Server::XdgForeignInterface::transientChanged);
    QVERIFY(transientSpy.isValid());

    // Import the just exported window
    m_imported = m_importer->importTopLevel(m_exported->handle(), m_connection);
    QVERIFY(m_imported->isValid());

    QSignalSpy childSurfaceInterfaceCreated(m_compositorInterface, &KWayland::Server::CompositorInterface::surfaceCreated);
    QVERIFY(serverSurfaceCreated.isValid());
    m_childSurface = m_compositor->createSurface();
    QVERIFY(childSurfaceInterfaceCreated.wait());
    m_childSurfaceInterface = childSurfaceInterfaceCreated.first().first().value<KWayland::Server::SurfaceInterface *>();
    m_childSurface->commit(Surface::CommitFlag::None);

    m_imported->setParentOf(m_childSurface);
    QVERIFY(transientSpy.wait());

    QCOMPARE(transientSpy.first().first().value<KWayland::Server::SurfaceInterface *>(), m_childSurfaceInterface.data());
    QCOMPARE(transientSpy.first().at(1).value<KWayland::Server::SurfaceInterface *>(), m_exportedSurfaceInterface.data());

    // transientFor api
    QCOMPARE(m_foreignInterface->transientFor(m_childSurfaceInterface), m_exportedSurfaceInterface.data());
}

void TestForeign::testExport()
{
    doExport();
}

void TestForeign::testDeleteImported()
{
    doExport();

    QSignalSpy transientSpy(m_foreignInterface, &KWayland::Server::XdgForeignInterface::transientChanged);

    QVERIFY(transientSpy.isValid());
    m_imported->deleteLater();
    m_imported = nullptr;

    QVERIFY(transientSpy.wait());

    QCOMPARE(transientSpy.first().first().value<KWayland::Server::SurfaceInterface *>(), m_childSurfaceInterface.data());
    QVERIFY(!transientSpy.first().at(1).value<KWayland::Server::SurfaceInterface *>());
    QVERIFY(!m_foreignInterface->transientFor(m_childSurfaceInterface));
}

void TestForeign::testDeleteChildSurface()
{
    doExport();

    QSignalSpy transientSpy(m_foreignInterface, &KWayland::Server::XdgForeignInterface::transientChanged);

    QVERIFY(transientSpy.isValid());
    m_childSurface->deleteLater();

    QVERIFY(transientSpy.wait());

    // when the client surface dies, the server one will eventually die too
    QSignalSpy surfaceDestroyedSpy(m_childSurfaceInterface, &QObject::destroyed);
    QVERIFY(surfaceDestroyedSpy.wait());

    QVERIFY(!transientSpy.first().at(0).value<KWayland::Server::SurfaceInterface *>());
    QCOMPARE(transientSpy.first().at(1).value<KWayland::Server::SurfaceInterface *>(), m_exportedSurfaceInterface.data());
}

void TestForeign::testDeleteParentSurface()
{
    doExport();

    QSignalSpy transientSpy(m_foreignInterface, &KWayland::Server::XdgForeignInterface::transientChanged);

    QVERIFY(transientSpy.isValid());
    m_exportedSurface->deleteLater();

    QSignalSpy exportedSurfaceDestroyedSpy(m_exportedSurfaceInterface.data(), &QObject::destroyed);
    QVERIFY(exportedSurfaceDestroyedSpy.isValid());
    exportedSurfaceDestroyedSpy.wait();

    QVERIFY(transientSpy.wait());

    QCOMPARE(transientSpy.first().first().value<KWayland::Server::SurfaceInterface *>(), m_childSurfaceInterface.data());
    QVERIFY(!transientSpy.first().at(1).value<KWayland::Server::SurfaceInterface *>());
    QVERIFY(!m_foreignInterface->transientFor(m_childSurfaceInterface));
}

void TestForeign::testDeleteExported()
{
    doExport();

    QSignalSpy transientSpy(m_foreignInterface, &KWayland::Server::XdgForeignInterface::transientChanged);
    QSignalSpy destroyedSpy(m_imported.data(), &KWayland::Client::XdgImported::importedDestroyed);

    QVERIFY(transientSpy.isValid());
    m_exported->deleteLater();
    m_exported = nullptr;

    QVERIFY(transientSpy.wait());
    QVERIFY(destroyedSpy.wait());

    QCOMPARE(transientSpy.first().first().value<KWayland::Server::SurfaceInterface *>(), m_childSurfaceInterface.data());
    QVERIFY(!transientSpy.first().at(1).value<KWayland::Server::SurfaceInterface *>());
    QVERIFY(!m_foreignInterface->transientFor(m_childSurfaceInterface));

    QVERIFY(!m_imported->isValid());
}

void TestForeign::testExportTwoTimes()
{
    doExport();

    // Export second window
    KWayland::Client::XdgExported *exported2 = m_exporter->exportTopLevel(m_exportedSurface, m_connection);
    QVERIFY(exported2->handle().isEmpty());
    QSignalSpy doneSpy(exported2, &XdgExported::done);
    QVERIFY(doneSpy.wait());
    QVERIFY(!exported2->handle().isEmpty());

    QSignalSpy transientSpy(m_foreignInterface, &KWayland::Server::XdgForeignInterface::transientChanged);
    QVERIFY(transientSpy.isValid());

    // Import the just exported window
    KWayland::Client::XdgImported *imported2 = m_importer->importTopLevel(exported2->handle(), m_connection);
    QVERIFY(imported2->isValid());

    // create a second child surface
    QSignalSpy serverSurfaceCreated(m_compositorInterface, &KWayland::Server::CompositorInterface::surfaceCreated);
    QVERIFY(serverSurfaceCreated.isValid());

    KWayland::Client::Surface *childSurface2 = m_compositor->createSurface();
    QVERIFY(serverSurfaceCreated.wait());

    KWayland::Server::SurfaceInterface *childSurface2Interface = serverSurfaceCreated.first().first().value<KWayland::Server::SurfaceInterface *>();

    imported2->setParentOf(childSurface2);
    QVERIFY(transientSpy.wait());

    QCOMPARE(transientSpy.first().first().value<KWayland::Server::SurfaceInterface *>(), childSurface2Interface);
    QCOMPARE(transientSpy.first().at(1).value<KWayland::Server::SurfaceInterface *>(), m_exportedSurfaceInterface.data());

    // transientFor api
    // check the old relationship is still here
    QCOMPARE(m_foreignInterface->transientFor(m_childSurfaceInterface), m_exportedSurfaceInterface.data());
    // check the new relationship
    QCOMPARE(m_foreignInterface->transientFor(childSurface2Interface), m_exportedSurfaceInterface.data());
}

void TestForeign::testImportTwoTimes()
{
    doExport();

    QSignalSpy transientSpy(m_foreignInterface, &KWayland::Server::XdgForeignInterface::transientChanged);
    QVERIFY(transientSpy.isValid());

    // Import another time the exported window
    KWayland::Client::XdgImported *imported2 = m_importer->importTopLevel(m_exported->handle(), m_connection);
    QVERIFY(imported2->isValid());

    // create a second child surface
    QSignalSpy serverSurfaceCreated(m_compositorInterface, &KWayland::Server::CompositorInterface::surfaceCreated);
    QVERIFY(serverSurfaceCreated.isValid());

    KWayland::Client::Surface *childSurface2 = m_compositor->createSurface();
    QVERIFY(serverSurfaceCreated.wait());

    KWayland::Server::SurfaceInterface *childSurface2Interface = serverSurfaceCreated.first().first().value<KWayland::Server::SurfaceInterface *>();

    imported2->setParentOf(childSurface2);
    QVERIFY(transientSpy.wait());

    QCOMPARE(transientSpy.first().first().value<KWayland::Server::SurfaceInterface *>(), childSurface2Interface);
    QCOMPARE(transientSpy.first().at(1).value<KWayland::Server::SurfaceInterface *>(), m_exportedSurfaceInterface.data());

    // transientFor api
    // check the old relationship is still here
    QCOMPARE(m_foreignInterface->transientFor(m_childSurfaceInterface), m_exportedSurfaceInterface.data());
    // check the new relationship
    QCOMPARE(m_foreignInterface->transientFor(childSurface2Interface), m_exportedSurfaceInterface.data());
}

QTEST_GUILESS_MAIN(TestForeign)
#include "test_xdg_foreign.moc"

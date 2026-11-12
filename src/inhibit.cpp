#include "inhibit.h"

#include <QGuiApplication>
#include <cstring>
#include <wayland-client.h>

// The one private header in Showtime: Qt hands out a window's wl_surface only
// through QPlatformNativeInterface. Calling it is a virtual call, not a link
// against Qt's private symbols, so the package is not tied to one Qt version.
#include <qpa/qplatformnativeinterface.h>

#include "idle-inhibit-unstable-v1-client-protocol.h"

namespace {
const wl_registry_listener registryListener = {&IdleInhibitor::registryGlobal, &IdleInhibitor::registryGlobalRemove};
}

void IdleInhibitor::registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface,
                                   uint32_t) {
    auto *self = static_cast<IdleInhibitor *>(data);
    if (std::strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name) == 0 && !self->m_manager) {
        self->m_manager = static_cast<zwp_idle_inhibit_manager_v1 *>(
            wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1));
        self->m_managerName = name;
    }
}

void IdleInhibitor::registryGlobalRemove(void *data, wl_registry *, uint32_t name) {
    auto *self = static_cast<IdleInhibitor *>(data);
    if (self->m_manager && name == self->m_managerName) {
        self->release();
        zwp_idle_inhibit_manager_v1_destroy(self->m_manager);
        self->m_manager = nullptr;
    }
}

IdleInhibitor::IdleInhibitor(QObject *parent) : QObject(parent) {
    auto *wayland = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (!wayland || !wayland->display())
        return;
    // A queue of Showtime's own, so asking for the globals never dispatches
    // events Qt's Wayland thread is waiting for.
    wl_display *display = wayland->display();
    m_queue = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), m_queue);
    m_registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(m_registry, &registryListener, this);
    wl_display_roundtrip_queue(display, m_queue);
}

IdleInhibitor::~IdleInhibitor() {
    release();
    if (m_manager)
        zwp_idle_inhibit_manager_v1_destroy(m_manager);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_queue)
        wl_event_queue_destroy(m_queue);
    auto *wayland = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
    if (wayland && wayland->display())
        wl_display_flush(wayland->display());
}

void IdleInhibitor::setActive(bool active) {
    if (m_active == active)
        return;
    m_active = active;
    update();
    emit activeChanged();
}

void IdleInhibitor::setWindow(QWindow *window) {
    if (m_window == window)
        return;
    if (m_window)
        disconnect(m_window, nullptr, this, nullptr);
    release();
    m_window = window;
    // The surface goes away when the window is hidden and a new one comes
    // with it, so the inhibitor follows.
    if (m_window)
        connect(m_window, &QWindow::visibleChanged, this, [this] {
            release();
            update();
        });
    update();
    emit windowChanged();
}

void IdleInhibitor::release() {
    if (m_inhibitor) {
        zwp_idle_inhibitor_v1_destroy(m_inhibitor);
        m_inhibitor = nullptr;
    }
    m_surface = nullptr;
}

void IdleInhibitor::update() {
    if (!m_active || !m_manager || !m_window || !m_window->isVisible()) {
        release();
        return;
    }
    auto *native = QGuiApplication::platformNativeInterface();
    void *surface = native ? native->nativeResourceForWindow("surface", m_window) : nullptr;
    if (surface == m_surface && m_inhibitor)
        return;
    release();
    if (!surface)
        return;
    m_inhibitor = zwp_idle_inhibit_manager_v1_create_inhibitor(m_manager, static_cast<wl_surface *>(surface));
    m_surface = surface;
    if (auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
        wl_display_flush(wayland->display());
}

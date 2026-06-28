/**
 * @file slplugin_daemon.cpp
 * @brief Multi-connection host driver: one process serving N plugin tabs.
 *
 * The shared tab-manager daemon. Where slplugin_run() serves a single parent
 * connection, this serves many: each parent that wants a tab connects to the
 * daemon's control port and sends its own listen port as decimal text + '\n';
 * the daemon spawns an LLPluginProcessChild that connects back to that port (one
 * tab). Because every tab lives in this one process, they transparently share a
 * single CEF runtime (dullahan_runtime - see Phase 1), which is the whole point.
 *
 * The first tab's parent port is passed at launch so it is served without the
 * control channel. The control port is written to the rendezvous file so later
 * parents can discover a running daemon (the viewer-side discover-or-spawn that
 * drives this is built separately).
 *
 * This driver is plugin-agnostic; the CEF host (SLPluginCEF) selects it via its
 * --daemon argument.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Alchemy Viewer Project.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llpluginprocesschild.h"
#include "llplugininstance.h"

#include "llapr.h"
#include "lltimer.h"
#include "llstring.h"

#include "apr_network_io.h"

#include <vector>
#include <fstream>
#include <cstdio>

// Idle this many seconds with zero live tabs before the daemon exits, so a brief
// gap between the last tab closing and the next opening keeps the warm runtime.
static const F64 DAEMON_IDLE_TIMEOUT = 10.0;

// Hard ceiling on concurrent tabs in one daemon. The viewer is the primary cap
// (PluginInstancesTotal -> mMaxIntances keeps excess media PRIORITY_UNLOADED, so
// no media source -> no tab); this is only a self-protection backstop so a buggy
// or hostile parent cannot make one process spawn unbounded browsers. Set well
// above any realistic viewer cap.
static const size_t DAEMON_MAX_TABS = 64;

namespace
{
    // Create a non-blocking TCP listen socket on the loopback for parents to
    // register tabs on. Returns the socket (and its bound port) or nullptr.
    apr_socket_t* createControlListener(U32& out_port)
    {
        out_port = 0;
        apr_socket_t* sock = nullptr;
        if (apr_socket_create(&sock, APR_INET, SOCK_STREAM, APR_PROTO_TCP, gAPRPoolp) != APR_SUCCESS)
        {
            return nullptr;
        }

        apr_socket_opt_set(sock, APR_SO_REUSEADDR, 1);

        apr_sockaddr_t* addr = nullptr;
        if (apr_sockaddr_info_get(&addr, "127.0.0.1", APR_INET, 0, 0, gAPRPoolp) != APR_SUCCESS ||
            apr_socket_bind(sock, addr) != APR_SUCCESS ||
            apr_socket_listen(sock, SOMAXCONN) != APR_SUCCESS)
        {
            apr_socket_close(sock);
            return nullptr;
        }

        apr_sockaddr_t* bound = nullptr;
        if (apr_socket_addr_get(&bound, APR_LOCAL, sock) != APR_SUCCESS)
        {
            apr_socket_close(sock);
            return nullptr;
        }
        out_port = bound->port;

        // Non-blocking so the accept poll below never stalls the tab loop.
        apr_socket_opt_set(sock, APR_SO_NONBLOCK, 1);
        apr_socket_timeout_set(sock, 0);
        return sock;
    }

    // Read the decimal listen-port a registering parent sent, spawn a tab for it.
    void acceptRegistration(apr_socket_t* listener, std::vector<LLPluginProcessChild*>& tabs)
    {
        apr_socket_t* incoming = nullptr;
        if (apr_socket_accept(&incoming, listener, gAPRPoolp) != APR_SUCCESS || !incoming)
        {
            return;   // APR_EAGAIN etc. - no pending registration
        }

        // Self-protection backstop (see DAEMON_MAX_TABS): refuse to grow past the
        // ceiling. The parent's connect-back never gets a tab, so it heartbeat-
        // times-out and reports the media failed - which the viewer should have
        // prevented via its own instance cap. Just drop the registration.
        if (tabs.size() >= DAEMON_MAX_TABS)
        {
            LL_WARNS("slplugin") << "daemon: tab ceiling (" << DAEMON_MAX_TABS
                                 << ") reached; refusing registration" << LL_ENDL;
            apr_socket_close(incoming);
            return;
        }

        // Blocking read of the short port line (the registration is tiny).
        apr_socket_timeout_set(incoming, 1000000);   // 1s
        char buf[32] = {0};
        apr_size_t len = sizeof(buf) - 1;
        if (apr_socket_recv(incoming, buf, &len) == APR_SUCCESS && len > 0)
        {
            buf[len] = '\0';
            std::string s(buf);
            LLStringUtil::trim(s);
            U32 parent_port = 0;
            if (!s.empty() && LLStringUtil::convertToU32(s, parent_port) && parent_port)
            {
                LLPluginProcessChild* tab = new LLPluginProcessChild();
                tab->setDaemonMode(true);
                tab->init(parent_port);
                tabs.push_back(tab);
                LL_INFOS("slplugin") << "daemon: new tab connecting to parent port " << parent_port
                                     << " (" << tabs.size() << " live)" << LL_ENDL;
            }
        }
        apr_socket_close(incoming);
    }
}

// Defined in slplugin.cpp.
int slplugin_run(U32 port);

// Defined per host (slplugin_cef.cpp for SLPluginCEF): the statically-linked
// plugin entry point. Each daemon tab must use it instead of dlopen()ing the
// plugin DLL, otherwise the tab loads a SECOND copy of the plugin (and its
// statically-linked dullahan) - a different dullahan_runtime than the host set
// the sandbox info on, so the tab runs unsandboxed via dullahan_host.
LLPluginInstance::pluginInitFunction ll_get_static_plugin_init();

// Run the daemon: serve the first tab (first_port) plus any later tabs that
// register on the control port (written to rendezvous_path). Returns when no
// tab has been live for DAEMON_IDLE_TIMEOUT.
int slplugin_daemon_run(U32 first_port, const std::string& rendezvous_path_str)
{
    // Register the statically-linked plugin so every tab's LLPluginInstance::load
    // calls it directly instead of dlopen()ing the plugin DLL. Without this the
    // daemon tabs load a separate plugin+dullahan copy whose runtime never got
    // the host's sandbox info (the cause of the daemon running unsandboxed).
    // slplugin_run() does the same for the single-tab host.
    LLPluginInstance::setStaticInitFunction(ll_get_static_plugin_init());

    U32 control_port = 0;
    apr_socket_t* control = createControlListener(control_port);
    if (!control)
    {
        // No control channel: fall back to single-tab behaviour so the first
        // parent still gets served rather than failing outright.
        LL_WARNS("slplugin") << "daemon: control listener failed; serving a single tab" << LL_ENDL;
        return slplugin_run(first_port);
    }

    std::filesystem::path rendezvous_path = fsyspath(rendezvous_path_str);
    std::filesystem::path rendezvous_lock_path = fsyspath(rendezvous_path_str + ".lock");

    // Publish the control port for discover-or-spawn. Write then flush+close so a
    // racing parent reads a complete value.
    {
        llofstream rv(rendezvous_path, std::ios::trunc);
        rv << control_port << std::endl;
    }
    // The rendezvous is now published, so release the spawn lock the launching
    // parent took - other parents can stop waiting and register.
    LLFile::remove(rendezvous_lock_path);
    LL_INFOS("slplugin") << "daemon: control port " << control_port
                         << " -> " << rendezvous_path << LL_ENDL;

    std::vector<LLPluginProcessChild*> tabs;
    {
        LLPluginProcessChild* first = new LLPluginProcessChild();
        first->setDaemonMode(true);
        first->init(first_port);
        tabs.push_back(first);
    }

    LLTimer idle_timer;
    bool idle_running = false;

    while (true)
    {
        acceptRegistration(control, tabs);

        // Service every tab, reaping finished ones.
        for (std::vector<LLPluginProcessChild*>::iterator it = tabs.begin(); it != tabs.end();)
        {
            LLPluginProcessChild* tab = *it;
            tab->idle();
            tab->pump();
            if (tab->isDone())
            {
                delete tab;
                it = tabs.erase(it);
                LL_INFOS("slplugin") << "daemon: tab closed (" << tabs.size() << " live)" << LL_ENDL;
            }
            else
            {
                ++it;
            }
        }

        if (tabs.empty())
        {
            if (!idle_running)
            {
                idle_timer.reset();
                idle_running = true;
            }
            else if (idle_timer.getElapsedTimeF64() > DAEMON_IDLE_TIMEOUT)
            {
                break;
            }
        }
        else
        {
            idle_running = false;
        }

        ms_sleep(10);
    }

    apr_socket_close(control);
    LLFile::remove(rendezvous_path);
    LL_INFOS("slplugin") << "daemon: idle, exiting" << LL_ENDL;
    return 0;
}

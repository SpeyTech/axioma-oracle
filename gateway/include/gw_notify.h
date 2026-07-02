/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 Spey Systems Ltd (SC889983)
 *
 * gw_notify.h - hand-rolled sd_notify readiness signal (E2).
 *
 * Type=simple reports ready at fork, before the socket exists; the
 * readiness race fired three times during acceptance. The fix is
 * Type=notify with a single "READY=1" datagram to $NOTIFY_SOCKET,
 * sent after the listening socket is bound. One datagram does not
 * justify a libsystemd link, consistent with the estate posture.
 */
#ifndef GW_NOTIFY_H
#define GW_NOTIFY_H

/* Send "READY=1" to $NOTIFY_SOCKET. Returns 0 when the datagram is
 * sent, or when NOTIFY_SOCKET is unset (a manual run is a no-op).
 * Returns -1 when the socket is set but the send fails; the caller
 * logs and continues, since the service itself is up. */
int gw_notify_ready(void);

#endif /* GW_NOTIFY_H */

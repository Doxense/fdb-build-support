/*
 * ClusterInterface.h
 *
 * This source file is part of the FoundationDB open source project
 *
 * Copyright 2013-2018 Apple Inc. and the FoundationDB project authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FDBCLIENT_ClusterInterface_H
#define FDBCLIENT_ClusterInterface_H
#pragma once

#include "fdbclient/FDBTypes.h"
#include "fdbrpc/FailureMonitor.h"
#include "fdbclient/Status.h"
#include "fdbclient/ClientDBInfo.h"
#include "fdbclient/ClientWorkerInterface.h"

struct ClusterInterface {
	RequestStream< struct OpenDatabaseRequest > openDatabase;
	RequestStream< struct FailureMonitoringRequest > failureMonitoring;
	RequestStream< struct StatusRequest > databaseStatus;
	RequestStream< ReplyPromise<Void> > ping;
	RequestStream< struct GetClientWorkersRequest > getClientWorkers;
	RequestStream< struct ForceRecoveryRequest > forceRecovery;

	bool operator == (ClusterInterface const& r) const { return id() == r.id(); }
	bool operator != (ClusterInterface const& r) const { return id() != r.id(); }
	UID id() const { return openDatabase.getEndpoint().token; }
	NetworkAddress address() const { return openDatabase.getEndpoint().getPrimaryAddress(); }

	void initEndpoints() {
		openDatabase.getEndpoint( TaskClusterController );
		failureMonitoring.getEndpoint( TaskFailureMonitor );
		databaseStatus.getEndpoint( TaskClusterController );
		ping.getEndpoint( TaskClusterController );
		getClientWorkers.getEndpoint( TaskClusterController );
		forceRecovery.getEndpoint( TaskClusterController );
	}

	template <class Ar>
	void serialize( Ar& ar ) {
		serializer(ar, openDatabase, failureMonitoring, databaseStatus, ping, getClientWorkers, forceRecovery);
	}
};

struct ClientVersionRef {
	StringRef clientVersion;
	StringRef sourceVersion;
	StringRef protocolVersion;

	ClientVersionRef() {
		initUnknown();
	}

	ClientVersionRef(Arena &arena, ClientVersionRef const& cv) : clientVersion(arena, cv.clientVersion), sourceVersion(arena, cv.sourceVersion), protocolVersion(arena, cv.protocolVersion) {}
	ClientVersionRef(std::string versionString) {
		size_t index = versionString.find(",");
		if(index == versionString.npos) {
			initUnknown();
			return;
		}

		clientVersion = StringRef((uint8_t*)&versionString[0], index);

		size_t nextIndex = versionString.find(",", index+1);
		if(index == versionString.npos) {
			initUnknown();
			return;
		}

		sourceVersion = StringRef((uint8_t*)&versionString[index+1], nextIndex-(index+1));
		protocolVersion = StringRef((uint8_t*)&versionString[nextIndex+1], versionString.length()-(nextIndex+1));
	}

	void initUnknown() {
		clientVersion = LiteralStringRef("Unknown");
		sourceVersion = LiteralStringRef("Unknown");
		protocolVersion = LiteralStringRef("Unknown");
	}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, clientVersion, sourceVersion, protocolVersion);
	}

	size_t expectedSize() const { return clientVersion.size() + sourceVersion.size() + protocolVersion.size(); }

	bool operator<(const ClientVersionRef& rhs) const {
		if(protocolVersion != rhs.protocolVersion) {
			return protocolVersion < rhs.protocolVersion;
		}

		// These comparisons are arbitrary because they aren't ordered
		if(clientVersion != rhs.clientVersion) {
			return clientVersion < rhs.clientVersion;
		}

		return sourceVersion < rhs.sourceVersion;
	}
};

struct OpenDatabaseRequest {
	// Sent by the native API to the cluster controller to open a database and track client
	//   info changes.  Returns immediately if the current client info id is different from
	//   knownClientInfoID; otherwise returns when it next changes (or perhaps after a long interval)
	Arena arena;
	StringRef issues, traceLogGroup;
	VectorRef<ClientVersionRef> supportedVersions;
	bool client_tls_configured;
	UID knownClientInfoID;
	ReplyPromise< struct ClientDBInfo > reply;

	template <class Ar>
	void serialize(Ar& ar) {
		ASSERT( ar.protocolVersion() >= 0x0FDB00A400040001LL );
		serializer(ar, issues, supportedVersions, client_tls_configured, traceLogGroup, knownClientInfoID, reply, arena);
	}
};

struct SystemFailureStatus {
	NetworkAddressList addresses;
	FailureStatus status;

	SystemFailureStatus() {}
	SystemFailureStatus( NetworkAddressList const& a, FailureStatus const& s ) : addresses(a), status(s) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, addresses, status);
	}
};

struct FailureMonitoringRequest {
	// Sent by all participants to the cluster controller reply.clientRequestIntervalMS
	//   ms after receiving the previous reply.
	// Provides the controller the self-diagnosed status of the sender, and also
	//   requests the status of other systems.  Failure to timely send one of these implies
	//   a failed status.
	// If !senderStatus.present(), the sender wants to receive the latest failure information
	//   but doesn't want to be monitored.
	// The failureInformationVersion returned in reply should be passed back to the
	//   next request to facilitate delta compression of the failure information.

	Optional<FailureStatus> senderStatus;
	Version failureInformationVersion;
	NetworkAddressList addresses;
	ReplyPromise< struct FailureMonitoringReply > reply;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, senderStatus, failureInformationVersion, addresses, reply);
	}
};

struct FailureMonitoringReply {
	VectorRef< SystemFailureStatus > changes;
	Version failureInformationVersion;
	bool allOthersFailed;							// If true, changes are relative to all servers being failed, otherwise to the version given in the request
	int clientRequestIntervalMS,        // after this many milliseconds, send another request
		considerServerFailedTimeoutMS;  // after this many additional milliseconds, consider the ClusterController itself to be failed
	Arena arena;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, changes, failureInformationVersion, allOthersFailed, clientRequestIntervalMS, considerServerFailedTimeoutMS, arena);
	}
};

struct StatusRequest {
	ReplyPromise< struct StatusReply > reply;

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, reply);
	}
};

struct StatusReply {
	StatusObject statusObj;
	std::string statusStr;

	StatusReply() {}
	explicit StatusReply(StatusObject obj) : statusObj(obj), statusStr(json_spirit::write_string(json_spirit::mValue(obj))) {}
	explicit StatusReply(std::string &&text) : statusStr(text) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, statusStr);
		if( ar.isDeserializing ) {
			json_spirit::mValue mv;
			if(g_network->isSimulated()) {
				mv = readJSONStrictly(statusStr);
			}
			else {
				// In non-simulation allow errors because some status data is better than no status data
				json_spirit::read_string( statusStr, mv );
			}
			statusObj = std::move(mv.get_obj());
		}
	}
};

struct GetClientWorkersRequest {
	ReplyPromise<vector<ClientWorkerInterface>> reply;

	GetClientWorkersRequest() {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, reply);
	}
};

struct ForceRecoveryRequest {
	Key dcId;
	ReplyPromise<Void> reply;

	ForceRecoveryRequest() {}
	explicit ForceRecoveryRequest(Key dcId) : dcId(dcId) {}

	template <class Ar>
	void serialize(Ar& ar) {
		serializer(ar, dcId, reply);
	}
};

#endif

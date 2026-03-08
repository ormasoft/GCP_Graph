/*
Assignment: GCP Permissions Graph

	The task is to analyze access permissions in a Google Cloud–like hierarchy.

	The system contains :
-Resources(Organization, Folder, Project)
- Identities(Users, Groups, Service Accounts)

Resources form a hierarchy :
Organization->Folder->Project.

Identities can receive roles on resources, and roles are inherited down
the hierarchy.Groups may also contain users.

Graph model
---------- -
The data is represented as a directed graph.

Node types :
-RESOURCE : a cloud resource(organization, folder, project)
- IDENTITY : a user, group, or service account

Edge types :
-POINTER_TO_PARENT : resource->parent resource
- HAS_ROLE : identity->resource(metadata = role name)
- MEMBER_OF : identity->group

Definitions
---------- -
Asset
A cloud resource(Organization / Folder / Project).

Metadata
Extra information attached to an edge.

In the current implementation :
-HAS_ROLE edge :
metadata = role name
examples : roles / viewer, roles / editor, roles / owner
	- POINTER_TO_PARENT edge :
metadata is currently empty
- MEMBER_OF edge :
metadata is currently empty

Design choices
--------------
The graph keeps :
-nodes : a list of all nodes
- edges : a list of all edges
- id_to_node : a map for direct lookup by node id, allowing O(1) average access

The list containers match the assignment structure.
The map is used for efficient access by id.
Traversal itself is done through adjacency lists stored on each node :
-out_edges(used to find parent resources in Task 2 and Task 4, and to find role assignments from identities to resources)
- in_edges(used to find which identities which HAS_ROLE on a resource in Task 4)

Implementation summary
----------------------
The solution builds a directed graph of resources and identities.

Resources are connected to their parent resource.
Identities are connected to resources through HAS_ROLE assignments.
If group membership exists, identities may also connect to groups.

Queries are implemented using simple graph traversals.
(See Graph model example below)


Task 2
------
Goal:
Return the hierarchy of a given resource.

Input :
	graph, resource_id

	Output :
A list of ancestor resource ids, starting from the direct parent and continuing up to the root.

How it works :
Follow POINTER_TO_PARENT edges upward until there is no parent.

Complexity :
	O(h), where h is the hierarchy depth.


	Task 3
	------
	Goal :
	Return the effective permissions of a given identity.

	Input :
	graph, user_id

	Output :
A list of tuples :
(resource_id, asset_type, role)

Meaning :
	Each tuple describes one resource the identity can access, the type of that resource, and the effective role on it.

	How it works :
1. Collect roles assigned directly to the user.
2. Also collect roles assigned through group membership.
3. For each assigned role, propagate it down the resource hierarchy to all descendant resources.

Complexity :
	O(R + D) in the reachable subgraph,
where R is the number of direct role / group edges inspected, and D is the number
of descendant hierarchy edges visited.

Task 4
------
Goal:
Return all identities that have access to a given resource.

Input :
	graph, resource_id

	Output :
A list of tuples :
(identity_id, role)

Meaning :
	Each tuple describes an identity that has access to the resource and the role by which the access is granted.

	How it works :
1. Start from the resource.
2. Walk up the hierarchy to all ancestor resources.
3. On each resource in that path, collect incoming HAS_ROLE edges.

Complexity :
	O(h + a), where h is the hierarchy depth and a is the number of role - assignment edges inspected along that path.

	Graph model example :
--------------------

Identity->Resource role assignment
---------------------------------- -
user : alice@test.com --HAS_ROLE-- > folders / 100   (metadata = "roles/editor")

	Resource hierarchy
	------------------
	projects / 200 --POINTER_TO_PARENT-- > folders / 100
	folders / 100 --POINTER_TO_PARENT-- > organizations / 1

	Group membership
	----------------
	user:bob@test.com --MEMBER_OF-- > group:admins@test.com
	group : admins@test.com --HAS_ROLE-- > organizations / 1
*/

#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <stack>
#include <set>
#include <tuple>
#include <algorithm>

using namespace std;

using NodeId = std::string;

// --- 1. Base Library (Generic Graph) ---
// This layer is agnostic to GCP logic and can be reused for other domains.

class Node;

class Edge {
public:
	weak_ptr<Node> from;
	weak_ptr<Node> to;
	int edgeType; // Generic integer type to be defined by subclasses
	string metadata;

	Edge(weak_ptr<Node> f, weak_ptr<Node> t, int type, string meta = "")
		: from(f), to(t), edgeType(type), metadata(meta) {}

	virtual ~Edge() = default;
};

class Node {
public:
	NodeId id;
	int nodeType; // Generic integer type
	vector<shared_ptr<Edge>> outEdges;

	// Use weak_ptr for in-edges to prevent circular reference memory leaks
	vector<weak_ptr<Edge>> inEdges;

	Node(NodeId id, int type) : id(id), nodeType(type) {}
	virtual ~Node() = default;
};

class Graph {
protected:
	vector<shared_ptr<Node>> nodes;
	vector<shared_ptr<Edge>> edges;
	unordered_map<NodeId, shared_ptr<Node>> idToNodePtr;

public:
	// Generic addEdge functionality
	virtual void addEdge(NodeId fromId, NodeId toId, int edgeType, string metadata = "") {
		if (idToNodePtr.find(fromId) == idToNodePtr.end()) {
			auto fromNode = make_shared<Node>(fromId, 0);
			nodes.push_back(fromNode);
			idToNodePtr[fromId] = fromNode;
		}

		if (idToNodePtr.find(toId) == idToNodePtr.end()) {
			auto toNode = make_shared<Node>(toId, 0);
			nodes.push_back(toNode);
			idToNodePtr[toId] = toNode;
		}

		auto edge = make_shared<Edge>(idToNodePtr[fromId], idToNodePtr[toId], edgeType, metadata);
		edges.push_back(edge);

		idToNodePtr[fromId]->outEdges.push_back(edge);
		idToNodePtr[toId]->inEdges.push_back(edge);
	}

	virtual ~Graph() = default;
};


// --- 2. GCP Specialized Implementation ---

using Role = std::string;
using AssetType = std::string;

enum class GCPNodeType { RESOURCE = 1, IDENTITY = 2 };
enum class GCPEdgeType { POINTER_TO_PARENT = 1, HAS_ROLE = 2, MEMBER_OF = 3 };

class GCPEdge : public Edge {
public:
	GCPEdge(weak_ptr<Node> f, weak_ptr<Node> t, GCPEdgeType type, Role meta = "")
		: Edge(f, t, (int)type, meta) {}
};

class GCPNode : public Node {
public:
	AssetType assetType; // e.g., "Folder", "Project", or "Organization"

	GCPNode(NodeId id, GCPNodeType type, AssetType assetType = "")
		: Node(id, (int)type), assetType(assetType) {}
};

class GCPGraph : public Graph {
private:
	using NodePtr = shared_ptr<GCPNode>;

	// Helper to remove prefixes like "user:", "group:", or "serviceAccount:"
	NodeId cleanId(NodeId id) const {
		size_t pos = id.find(':');
		return (pos != string::npos) ? id.substr(pos + 1) : id;
	}

	// Internal factory method to manage node lifecycle
	NodePtr getOrCreateNode(NodeId id, GCPNodeType nodeType, AssetType assetType = "") {
		id = cleanId(id);

		if (idToNodePtr.find(id) == idToNodePtr.end()) {
			auto newNode = make_shared<GCPNode>(id, nodeType, assetType);
			nodes.push_back(newNode);
			idToNodePtr[id] = newNode;
			return newNode;
		}

		auto existingNode = static_pointer_cast<GCPNode>(idToNodePtr[id]);
		if (!assetType.empty()) {
			// Update asset metadata if it was previously created as an "Unknown" placeholder
			existingNode->assetType = assetType;
			existingNode->nodeType = (int)nodeType; //to deal with cases that we have edge that its node isn't met yet (later line in the json)
		}
		return existingNode;
	}

public:
	//This is for the root organization node
	NodePtr getOrCreateResource(NodeId id, AssetType assetType = "") {
		return getOrCreateNode(id, GCPNodeType::RESOURCE, assetType);
	}

	// Override of generic addEdge that routes to GCP logic
	void addEdge(NodeId fromId, NodeId toId, int edgeType, string metadata = "") override {
		addGCPConnection(fromId, toId, (GCPEdgeType)edgeType, metadata);
	}

	// Core function to build connections in the graph
	void addGCPConnection(NodeId fromId, NodeId toId, GCPEdgeType edgeType,
		Role role = "", AssetType fromAsset = "", AssetType toAsset = "") {

		// Determine 'from' type:
		// Only POINTER_TO_PARENT starts from a RESOURCE.
		// HAS_ROLE and MEMBER_OF always start from an IDENTITY.
		GCPNodeType fromType = (edgeType == GCPEdgeType::POINTER_TO_PARENT)
			? GCPNodeType::RESOURCE : GCPNodeType::IDENTITY;

		// Determine 'to' type:
		// Only MEMBER_OF points to an IDENTITY (a Group).
		// HAS_ROLE and POINTER_TO_PARENT always point to a RESOURCE.
		GCPNodeType toType = (edgeType == GCPEdgeType::MEMBER_OF)
			? GCPNodeType::IDENTITY : GCPNodeType::RESOURCE;

		auto fromNode = getOrCreateNode(fromId, fromType, fromAsset);
		auto toNode = getOrCreateNode(toId, toType, toAsset);

		auto edge = make_shared<GCPEdge>(fromNode, toNode, edgeType, role);
		edges.push_back(edge);

		fromNode->outEdges.push_back(edge);
		toNode->inEdges.push_back(edge);
	}

	// --- Task 2: Get Ancestors ---
	// Traverses up the POINTER_TO_PARENT edges until the root is reached.
	vector<NodeId> getResourceHierarchy(NodeId resourceId) const {
		vector<NodeId> path;
		NodeId cleaned = cleanId(resourceId);

		if (idToNodePtr.find(cleaned) == idToNodePtr.end()) return path;

		auto current = static_pointer_cast<GCPNode>(idToNodePtr.at(cleaned));
		while (current) {
			NodePtr parent = nullptr;
			for (auto& edge : current->outEdges) {
				if (edge->edgeType == (int)GCPEdgeType::POINTER_TO_PARENT) {
					parent = static_pointer_cast<GCPNode>(edge->to.lock());
					break;
				}
			}

			if (!parent) {
				break;
			}

			path.push_back(parent->id);
			current = parent;
		}
		return path;
	}

	// --- Task 3: Effective Permissions (Who has access to what?) ---
	// Returns: (ResourceId, AssetType, Role)
	vector<tuple<NodeId, AssetType, Role>> getUserPermissions(NodeId userId) const {
		userId = cleanId(userId);
		vector<tuple<NodeId, AssetType, Role>> result;
		if (idToNodePtr.find(userId) == idToNodePtr.end()) return result;

		// List of all identities to check (User + inherited Groups)
		vector<NodePtr> identities;
		identities.push_back(static_pointer_cast<GCPNode>(idToNodePtr.at(userId)));

		// Task 6 Support: Check for Group Membership
		for (auto& edge : idToNodePtr.at(userId)->outEdges) {
			if (edge->edgeType == (int)GCPEdgeType::MEMBER_OF) {
				if (auto group = static_pointer_cast<GCPNode>(edge->to.lock())) {
					identities.push_back(group);
				}
			}
		}

		struct State {
			NodePtr node;
			Role role;
		};

		stack<State> dfsStack;
		set<string> visited;

		// Collect direct roles from all identified identities
		for (auto& identity : identities) {
			for (auto& edge : identity->outEdges) {
				if (edge->edgeType == (int)GCPEdgeType::HAS_ROLE) {
					if (auto target = static_pointer_cast<GCPNode>(edge->to.lock())) {
						dfsStack.push({ target, edge->metadata });
					}
				}
			}
		}

		// DFS: Propagate permissions down the resource hierarchy (using inEdges)
		while (!dfsStack.empty()) {
			State curr = dfsStack.top();
			dfsStack.pop();

			string visitKey = curr.node->id + "::" + curr.role; //could do pair<NodeId, Role> visitKey = { curr.node->id, curr.role };
			if (visited.count(visitKey)) continue;
			visited.insert(visitKey);

			result.push_back({ curr.node->id, curr.node->assetType, curr.role });

			// Travel to children (those who point to this node as their parent)
			for (auto& weakEdge : curr.node->inEdges) {
				auto edge = weakEdge.lock();
				if (edge && edge->edgeType == (int)GCPEdgeType::POINTER_TO_PARENT) {
					if (auto childNode = static_pointer_cast<GCPNode>(edge->from.lock())) {
						dfsStack.push({ childNode, curr.role });
					}
				}
			}
		}
		return result;
	}

	// --- Task 4: Reverse Lookup (What has who?) ---
	// Given a resource, find all identities with permissions on it (including inherited ones).
	vector<pair<NodeId, Role>> getPermissionsForResource(NodeId resourceId) const {
		resourceId = cleanId(resourceId);
		vector<pair<NodeId, Role>> identitiesWithRoles;

		if (idToNodePtr.find(resourceId) == idToNodePtr.end()) return identitiesWithRoles;

		// 1. Find all ancestors of the resource (including itself)
		vector<NodePtr> nodesToInspect;
		auto current = static_pointer_cast<GCPNode>(idToNodePtr.at(resourceId));
		while (current) {
			nodesToInspect.push_back(current);
			NodePtr next = nullptr;
			for (auto& edge : current->outEdges) {
				if (edge->edgeType == (int)GCPEdgeType::POINTER_TO_PARENT) {
					next = static_pointer_cast<GCPNode>(edge->to.lock());
					break;
				}
			}
			current = next;
		}

		// 2. For each node in the path to root, find identities connected via HAS_ROLE
		for (auto& node : nodesToInspect) {
			for (auto& weakEdge : node->inEdges) {
				auto edge = weakEdge.lock();
				if (edge && edge->edgeType == (int)GCPEdgeType::HAS_ROLE) {
					if (auto identityNode = edge->from.lock()) {
						identitiesWithRoles.push_back({ identityNode->id, edge->metadata });
					}
				}
			}
		}
		return identitiesWithRoles;
	}
};

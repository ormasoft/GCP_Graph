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

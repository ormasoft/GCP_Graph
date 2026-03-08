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
public:

	//helper to debug the graph
	void debugPrintGraph() const
	{
		cout << "\n========== GCPGraph Debug Print ==========\n";

		cout << "Total nodes: " << nodes.size() << '\n';
		cout << "Total edges: " << edges.size() << '\n';

		cout << "\n--- Nodes ---\n";
		for (const auto& baseNode : nodes)
		{
			auto node = static_pointer_cast<GCPNode>(baseNode);

			cout << "Node ID      : " << node->id << '\n';
			cout << "Node Type    : "
				<< (node->nodeType == (int)GCPNodeType::RESOURCE ? "RESOURCE" : "IDENTITY")
				<< '\n';
			cout << "Asset Type   : " << (node->assetType.empty() ? "<none>" : node->assetType) << '\n';

			cout << "Outgoing edges:\n";
			if (node->outEdges.empty())
			{
				cout << "  <none>\n";
			}
			else
			{
				for (const auto& edge : node->outEdges)
				{
					auto toNode = edge->to.lock();

					cout << "  -> "
						<< (toNode ? toNode->id : "<expired>")
						<< " | EdgeType=" << edge->edgeType
						<< " | Metadata=" << edge->metadata
						<< '\n';
				}
			}

			cout << "Incoming edges:\n";
			if (node->inEdges.empty())
			{
				cout << "  <none>\n";
			}
			else
			{
				for (const auto& weakEdge : node->inEdges)
				{
					auto edge = weakEdge.lock();
					if (!edge)
					{
						cout << "  <- <expired edge>\n";
						continue;
					}

					auto fromNode = edge->from.lock();

					cout << "  <- "
						<< (fromNode ? fromNode->id : "<expired>")
						<< " | EdgeType=" << edge->edgeType
						<< " | Metadata=" << edge->metadata
						<< '\n';
				}
			}

			cout << "----------------------------------------\n";
		}

		cout << "\n--- Edges ---\n";
		for (const auto& edge : edges)
		{
			auto fromNode = edge->from.lock();
			auto toNode = edge->to.lock();

			cout << (fromNode ? fromNode->id : "<expired>")
				<< " --> "
				<< (toNode ? toNode->id : "<expired>")
				<< " | EdgeType=" << edge->edgeType
				<< " | Metadata=" << edge->metadata
				<< '\n';
		}
	}

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

// --- 3. Mock & Parsing Layer ---

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "json.hpp"

using namespace std;
using json = nlohmann::json;


struct RawRecord {
	NodeId id;
	AssetType assetType;
	NodeId parentId;
	vector<pair<NodeId, Role>> roles; // <userId, roleName>
};

static string trim(const string& s) {
	size_t start = s.find_first_not_of(" \t\r\n");
	if (start == string::npos) return "";

	size_t end = s.find_last_not_of(" \t\r\n");
	return s.substr(start, end - start + 1);
}

static string readWholeFile(const string& filePath) {
	ifstream in(filePath);
	if (!in) {
		throw runtime_error("Failed to open file: " + filePath);
	}

	ostringstream buffer;
	buffer << in.rdbuf();
	return buffer.str();
}

static NodeId extractParentIdFromAncestors(const json& ancestorsJson, const NodeId& selfId) {
	if (!ancestorsJson.is_array()) {
		return "";
	}

	// Preferred logic:
	// Find self in ancestors, and take the next entry as parent.
	for (size_t i = 0; i + 1 < ancestorsJson.size(); ++i) {
		if (ancestorsJson[i].is_string() && ancestorsJson[i].get<string>() == selfId) {
			if (ancestorsJson[i + 1].is_string()) {
				return ancestorsJson[i + 1].get<string>();
			}
		}
	}

	// Fallback:
	// If array has at least 2 elements, assume second one is the parent.
	if (ancestorsJson.size() >= 2 && ancestorsJson[1].is_string()) {
		return ancestorsJson[1].get<string>();
	}

	return "";
}

static vector<pair<NodeId, Role>> extractRoles(const json& recordJson) {
	vector<pair<NodeId, Role>> roles;

	if (!recordJson.contains("iam_policy")) {
		return roles;
	}

	const auto& iamPolicy = recordJson["iam_policy"];
	if (!iamPolicy.contains("bindings") || !iamPolicy["bindings"].is_array()) {
		return roles;
	}

	for (const auto& binding : iamPolicy["bindings"]) {
		if (!binding.contains("role") || !binding["role"].is_string()) {
			continue;
		}

		Role role = binding["role"].get<string>();

		if (!binding.contains("members") || !binding["members"].is_array()) {
			continue;
		}

		for (const auto& member : binding["members"]) {
			if (!member.is_string()) {
				continue;
			}
			roles.push_back({ member.get<string>(), role });
		}
	}

	return roles;
}

static AssetType simplifyAssetType(const string& fullType)
{
	// Example:
	// "cloudresourcemanager.googleapis.com/Project" ==> "Project"

	size_t pos = fullType.find_last_of('/');

	if (pos == string::npos)
		return fullType;

	return fullType.substr(pos + 1);
}

static NodeId simplifyResourceName(const string& rawName)
{
	// Examples:
	// "//cloudresourcemanager.googleapis.com/projects/production-data-99"
	//   => "projects/production-data-99"
	//
	// "//cloudresourcemanager.googleapis.com/folders/767216091627"
	//   => "folders/767216091627"
	//
	// "//cloudresourcemanager.googleapis.com/organizations/22832496360"
	//   => "organizations/22832496360"

	const string marker = "cloudresourcemanager.googleapis.com/";

	size_t pos = rawName.find(marker);
	if (pos != string::npos)
	{
		return rawName.substr(pos + marker.size());
	}

	// fallback:
	// if already short, keep as-is
	return rawName;
}

static RawRecord parseSingleRecord(const json& recordJson) {
	RawRecord record;

	record.id = simplifyResourceName(recordJson.at("name").get<string>());

	if (recordJson.contains("asset_type") && recordJson["asset_type"].is_string()) {
		record.assetType = simplifyAssetType(recordJson["asset_type"].get<string>());
	}
	else {
		record.assetType = "";
	}

	if (recordJson.contains("ancestors")) {
		record.parentId = extractParentIdFromAncestors(recordJson["ancestors"], record.id);
	}
	else {
		record.parentId = "";
	}

	record.roles = extractRoles(recordJson);

	return record;
}

vector<RawRecord> parseInputFile(const string& filePath) {
	vector<RawRecord> records;

	string content = readWholeFile(filePath);
	string trimmedContent = trim(content);

	if (trimmedContent.empty()) {
		return records;
	}

	// JSON array
	if (!trimmedContent.empty() && trimmedContent.front() == '[') {
		json root = json::parse(trimmedContent);

		if (!root.is_array()) {
			throw runtime_error("Expected JSON array at top level");
		}

		for (const auto& item : root) {
			records.push_back(parseSingleRecord(item));
		}

		return records;
	}

	// Otherwise: treat as JSONL
	istringstream input(trimmedContent);
	string line;

	while (getline(input, line)) {
		line = trim(line);
		if (line.empty()) {
			continue;
		}

		json item = json::parse(line);
		records.push_back(parseSingleRecord(item));
	}

	return records;
}

void printRecords(const vector<RawRecord>& records)
{
	cout << "\n========== Parsed RawRecords ==========\n";

	for (const auto& record : records)
	{
		cout << "Resource ID   : " << record.id << '\n';
		cout << "Asset Type    : " << record.assetType << '\n';
		cout << "Parent ID     : " << (record.parentId.empty() ? "<none>" : record.parentId) << '\n';

		cout << "Roles:\n";
		if (record.roles.empty())
		{
			cout << "  <none>\n";
		}
		else
		{
			for (const auto& roleMapping : record.roles)
			{
				cout << "  Identity: " << roleMapping.first
					<< " | Role: " << roleMapping.second << '\n';
			}
		}

		cout << "----------------------------------------\n";
	}
}


// Helper function to build the graph from parsed records
void buildFromRecords(GCPGraph& graph, const vector<RawRecord>& records) {
	for (auto& resource : records) {
		// Build hierarchy connection
		if (!resource.parentId.empty()) {
			graph.addGCPConnection(resource.id, resource.parentId, GCPEdgeType::POINTER_TO_PARENT, "", resource.assetType, "");
		}
		else {
			graph.getOrCreateResource(resource.id, resource.assetType);
		}

		// Build role connections
		for (auto& roleMapping : resource.roles) {
			graph.addGCPConnection(roleMapping.first, resource.id, GCPEdgeType::HAS_ROLE, roleMapping.second, "Identity", resource.assetType);
		}
	}
}

// --- 4. Demo Main ---

int main() {
	try {
		GCPGraph graph;

		// Change path if needed
		const string inputFile = "test.json";

		vector<RawRecord> records = parseInputFile(inputFile);
		cout << "Loaded records: " << records.size() << endl;
		printRecords(records);

		buildFromRecords(graph, records);
		graph.debugPrintGraph();

		// Example Task 2
		cout << "\n--- Task 2: Ancestors of projects/production-data-99 ---" << endl;
		auto ancestors = graph.getResourceHierarchy("projects/production-data-99");
		for (const auto& ancestor : ancestors) {
			cout << ancestor << endl;
		}

		// Example Task 3
		cout << "\n--- Task 3: Access of maya@test.authomize.com ---" << endl;
		auto accessList = graph.getUserPermissions("user:maya@test.authomize.com");
		for (const auto& item : accessList) {
			cout << "Resource: " << get<0>(item)
				<< " | Type: " << get<1>(item)
				<< " | Role: " << get<2>(item) << endl;
		}

		// Example Task 4
		cout << "\n--- Task 4: Who has access to folders/767216091627 ---" << endl;
		auto whoHas = graph.getPermissionsForResource("folders/767216091627");
		for (const auto& item : whoHas) {
			cout << "Identity: " << item.first
				<< " | Role: " << item.second << endl;
		}
	}
	catch (const exception& ex) {
		cerr << "ERROR: " << ex.what() << endl;
		return 1;
	}

	return 0;
}
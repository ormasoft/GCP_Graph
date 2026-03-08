"""
Assignment: GCP Permissions Graph

The task is to analyze access permissions in a Google Cloud–like hierarchy.

The system contains:
- Resources (Organization, Folder, Project)
- Identities (Users, Groups, Service Accounts)

Resources form a hierarchy:
Organization -> Folder -> Project.

Identities can receive roles on resources, and roles are inherited down
the hierarchy. Groups may also contain users.

Graph model
-----------
The data is represented as a directed graph.

Node types:
- RESOURCE: a cloud resource (organization, folder, project)
- IDENTITY: a user, group, or service account

Edge types:
- POINTER_TO_PARENT : resource -> parent resource
- HAS_ROLE          : identity -> resource (metadata = role name)
- MEMBER_OF         : identity -> group

Definitions
-----------
Asset
    A cloud resource (Organization / Folder / Project).

Metadata
    Extra information attached to an edge.

    In the current implementation:
    - HAS_ROLE edge:
        metadata = role name
        examples: roles/viewer, roles/editor, roles/owner
    - POINTER_TO_PARENT edge:
        metadata is currently empty
    - MEMBER_OF edge:
        metadata is currently empty

Design choices
--------------
The graph keeps:
- nodes     : a list of all nodes
- edges     : a list of all edges
- id_to_node: a map for direct lookup by node id, allowing O(1) average access

The list containers match the assignment structure.
The map is used for efficient access by id.
Traversal itself is done through adjacency lists stored on each node:
- out_edges (used to find parent resources in Task 2 and Task 4, and to find role assignments from identities to resources)
- in_edges  (used to find which identities which HAS_ROLE on a resource in Task 4)

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

Input:
    graph, resource_id

Output:
    A list of ancestor resource ids, starting from the direct parent and continuing up to the root.

How it works:
    Follow POINTER_TO_PARENT edges upward until there is no parent.

Complexity:
    O(h), where h is the hierarchy depth.


Task 3
------
Goal:
    Return the effective permissions of a given identity.

Input:
    graph, user_id

Output:
    A list of tuples:
        (resource_id, asset_type, role)

Meaning:
    Each tuple describes one resource the identity can access, the type of that resource, and the effective role on it.

How it works:
    1. Collect roles assigned directly to the user.
    2. Also collect roles assigned through group membership.
    3. For each assigned role, propagate it down the resource hierarchy to all descendant resources.

Complexity:
    O(R + D) in the reachable subgraph,
    where R is the number of direct role/group edges inspected, and D is the number
    of descendant hierarchy edges visited.

Task 4
------
Goal:
    Return all identities that have access to a given resource.

Input:
    graph, resource_id

Output:
    A list of tuples:
        (identity_id, role)

Meaning:
    Each tuple describes an identity that has access to the resource and the role by which the access is granted.

How it works:
    1. Start from the resource.
    2. Walk up the hierarchy to all ancestor resources.
    3. On each resource in that path, collect incoming HAS_ROLE edges.

Complexity:
    O(h + a), where h is the hierarchy depth and a is the number of role-assignment edges inspected along that path.

Graph model example:
--------------------

  Identity -> Resource role assignment
  -----------------------------------
  user:alice@test.com --HAS_ROLE--> folders/100   (metadata = "roles/editor")

  Resource hierarchy
  ------------------
  projects/200 --POINTER_TO_PARENT--> folders/100
  folders/100 --POINTER_TO_PARENT--> organizations/1

  Group membership
  ----------------
  user:bob@test.com --MEMBER_OF--> group:admins@test.com
  group:admins@test.com --HAS_ROLE--> organizations/1
"""

from dataclasses import dataclass, field
from enum import IntEnum
from typing import Dict, List, Optional, Tuple


NodeId = str
Role = str
AssetType = str


# --- 1. Base Library (Generic Graph) ---
# This layer is agnostic to GCP logic and can be reused for other domains.


@dataclass
class Edge:
    from_node: "Node"
    to_node: "Node"
    edge_type: int
    metadata: str = ""


@dataclass
class Node:
    id: NodeId
    node_type: int
    out_edges: List["Edge"] = field(default_factory=list)
    in_edges: List["Edge"] = field(default_factory=list)


class Graph:
    def __init__(self) -> None:
        self.nodes: List[Node] = []
        self.edges: List[Edge] = []
        self.id_to_node: Dict[NodeId, Node] = {}

    # Generic addEdge functionality
    def add_edge(self, from_id: NodeId, to_id: NodeId, edge_type: int, metadata: str = "") -> None:
        from_node = self.id_to_node.get(from_id)
        if from_node is None:
            from_node = Node(from_id, 0)
            self.nodes.append(from_node)
            self.id_to_node[from_id] = from_node

        to_node = self.id_to_node.get(to_id)
        if to_node is None:
            to_node = Node(to_id, 0)
            self.nodes.append(to_node)
            self.id_to_node[to_id] = to_node

        edge = Edge(from_node, to_node, edge_type, metadata)
        self.edges.append(edge)

        from_node.out_edges.append(edge)
        to_node.in_edges.append(edge)


# --- 2. GCP Specialized Implementation ---


class GCPNodeType(IntEnum):
    RESOURCE = 1
    IDENTITY = 2


class GCPEdgeType(IntEnum):
    POINTER_TO_PARENT = 1
    HAS_ROLE = 2
    MEMBER_OF = 3


@dataclass
class GCPEdge(Edge):
    pass


@dataclass
class GCPNode(Node):
    asset_type: AssetType = ""


class GCPGraph(Graph):
    # Helper to remove prefixes like "user:", "group:", or "serviceAccount:"
    def clean_id(self, node_id: NodeId) -> NodeId:
        pos = node_id.find(":")
        return node_id[pos + 1:] if pos != -1 else node_id

    # Internal factory method to manage node lifecycle
    def _get_or_create_node(
        self,
        node_id: NodeId,
        node_type: GCPNodeType,
        asset_type: AssetType = "",
    ) -> GCPNode:
        node_id = self.clean_id(node_id)

        existing_node = self.id_to_node.get(node_id)
        if existing_node is None:
            new_node = GCPNode(
                id=node_id,
                node_type=int(node_type),
                asset_type=asset_type,
            )
            self.nodes.append(new_node)
            self.id_to_node[node_id] = new_node
            return new_node

        gcp_node = existing_node

        # Update asset metadata if it was previously created as a placeholder
        if asset_type:
            gcp_node.asset_type = asset_type
            gcp_node.node_type = int(node_type)

        return gcp_node

    # Wrapper for creating a RESOURCE node (used when there is no parent)
    def get_or_create_resource(self, node_id: NodeId, asset_type: AssetType = "") -> GCPNode:
        return self._get_or_create_node(node_id, GCPNodeType.RESOURCE, asset_type)

    # Override of generic addEdge that routes to GCP logic
    def add_edge(self, from_id: NodeId, to_id: NodeId, edge_type: int, metadata: str = "") -> None:
        self.add_gcp_connection(from_id, to_id, GCPEdgeType(edge_type), metadata)

    # Core function to build connections in the graph
    def add_gcp_connection(
        self,
        from_id: NodeId,
        to_id: NodeId,
        edge_type: GCPEdgeType,
        role: Role = "",
        from_asset: AssetType = "",
        to_asset: AssetType = "",
    ) -> None:
        # Determine 'from' type:
        # Only POINTER_TO_PARENT starts from a RESOURCE.
        # HAS_ROLE and MEMBER_OF always start from an IDENTITY.
        from_type = (
            GCPNodeType.RESOURCE
            if edge_type == GCPEdgeType.POINTER_TO_PARENT
            else GCPNodeType.IDENTITY
        )

        # Determine 'to' type:
        # Only MEMBER_OF points to an IDENTITY (a Group).
        # HAS_ROLE and POINTER_TO_PARENT always point to a RESOURCE.
        to_type = (
            GCPNodeType.IDENTITY
            if edge_type == GCPEdgeType.MEMBER_OF
            else GCPNodeType.RESOURCE
        )

        from_node = self._get_or_create_node(from_id, from_type, from_asset)
        to_node = self._get_or_create_node(to_id, to_type, to_asset)

        edge = GCPEdge(from_node, to_node, int(edge_type), role)
        self.edges.append(edge)

        from_node.out_edges.append(edge)
        to_node.in_edges.append(edge)


# --- Task 2: Get Ancestors ---
# Traverses up the POINTER_TO_PARENT edges until the root is reached.
def get_resource_hierarchy(graph: GCPGraph, resource_id: NodeId) -> List[NodeId]:
    path: List[NodeId] = []
    cleaned = graph.clean_id(resource_id)

    current = graph.id_to_node.get(cleaned)
    if current is None:
        return path

    while current is not None:
        parent: Optional[GCPNode] = None

        for edge in current.out_edges:
            if edge.edge_type == int(GCPEdgeType.POINTER_TO_PARENT):
                parent = edge.to_node
                break

        if parent is None:
            break

        path.append(parent.id)
        current = parent

    return path


# --- Task 3: Effective Permissions (Who has access to what?) ---
# Returns: (ResourceId, AssetType, Role)
def get_user_permissions(graph: GCPGraph, user_id: NodeId) -> List[Tuple[NodeId, AssetType, Role]]:
    user_id = graph.clean_id(user_id)

    result: List[Tuple[NodeId, AssetType, Role]] = []

    user_node = graph.id_to_node.get(user_id)
    if user_node is None:
        return result

    # List of all identities to check (User + inherited Groups)
    identities: List[GCPNode] = [user_node]

    # Support group membership
    for edge in user_node.out_edges:
        if edge.edge_type == int(GCPEdgeType.MEMBER_OF):
            identities.append(edge.to_node)

    @dataclass
    class State:
        node: GCPNode
        role: Role

    dfs_stack: List[State] = []
    visited: set = set()

    # Collect direct roles
    for identity in identities:
        for edge in identity.out_edges:
            if edge.edge_type == int(GCPEdgeType.HAS_ROLE):
                dfs_stack.append(State(edge.to_node, edge.metadata))

    # DFS propagation
    while dfs_stack:

        curr = dfs_stack.pop()

        visit_key = (curr.node.id, curr.role)

        if visit_key in visited:
            continue

        visited.add(visit_key)

        result.append((curr.node.id, curr.node.asset_type, curr.role))

        for edge in curr.node.in_edges:
            if edge.edge_type == int(GCPEdgeType.POINTER_TO_PARENT):
                dfs_stack.append(State(edge.from_node, curr.role))

    return result


# --- Task 4: Reverse Lookup (What has who?) ---
# Given a resource, find all identities with permissions on it.
def get_permissions_for_resource(graph: GCPGraph, resource_id: NodeId) -> List[Tuple[NodeId, Role]]:
    resource_id = graph.clean_id(resource_id)

    identities_with_roles: List[Tuple[NodeId, Role]] = []

    current = graph.id_to_node.get(resource_id)
    if current is None:
        return identities_with_roles

    nodes_to_inspect: List[GCPNode] = []

    while current is not None:
        nodes_to_inspect.append(current)

        parent: Optional[GCPNode] = None
        for edge in current.out_edges:
            if edge.edge_type == int(GCPEdgeType.POINTER_TO_PARENT):
                parent = edge.to_node
                break

        current = parent

    for node in nodes_to_inspect:
        for edge in node.in_edges:
            if edge.edge_type == int(GCPEdgeType.HAS_ROLE):
                identities_with_roles.append((edge.from_node.id, edge.metadata))

    return identities_with_roles
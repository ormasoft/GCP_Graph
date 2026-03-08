# GCP Permissions Graph

This project analyzes access permissions in a Google Cloud--like
hierarchy.

The system models resources (Organization, Folder, Project) and
identities (Users, Groups, Service Accounts) as a graph, and answers
queries about permissions and inheritance.

Two implementations are provided:

-   **Python** -- final implementation intended for submission
-   **C++** -- development version with parsing, debug printing, and a
    demo `main()`

------------------------------------------------------------------------

# Problem Overview

In Google Cloud IAM:

-   Resources form a **hierarchy**

```{=html}
<!-- -->
```
    Organization
       └── Folder
             └── Project

-   Identities can receive **roles** on any resource.
-   Permissions **propagate downward** to all descendant resources.
-   Users may also belong to **groups** which grant additional
    permissions.

The goal is to build a graph representation of this structure and answer
queries about effective permissions.

------------------------------------------------------------------------

# Graph Model

The system is represented as a **directed graph**.

## Node Types

  Type       Description
  ---------- -------------------------------
  RESOURCE   Organization, Folder, Project
  IDENTITY   User, Group, Service Account

## Edge Types

  Edge                Meaning
  ------------------- --------------------------------------------
  POINTER_TO_PARENT   Resource → parent resource
  HAS_ROLE            Identity → resource (metadata = role name)
  MEMBER_OF           Identity → group

------------------------------------------------------------------------

# Graph Example

Example structure derived from the input data model:

    user:alice@test.com
            │
            └── HAS_ROLE (roles/editor)
                               │
                               ▼
                         folders/100
                               │
                               └── POINTER_TO_PARENT
                                          │
                                          ▼
                                    organizations/1
                               ▲
                               │
                               │ HAS_ROLE (roles/owner)
                               │
    group:admins@test.com ◄────┘
            ▲
            │
            └── MEMBER_OF
                 user:bob@test.com

    projects/200
        │
        └── POINTER_TO_PARENT
                   │
                   ▼
              folders/100

Meaning:

-   Alice has `roles/editor` on `folders/100`
-   Therefore Alice also has access to descendant resources under that
    folder
-   Bob inherits permissions through the `admins` group

------------------------------------------------------------------------

# Implemented Queries

## Task 2 --- Resource Hierarchy

Return the hierarchy of a resource.

Input

    resource_id

Output

    [parent, parent_of_parent, ..., root]

Example

    projects/200 → folders/100 → organizations/1

Complexity

    O(h)

where `h` is the hierarchy depth.

------------------------------------------------------------------------

## Task 3 --- Effective Permissions

Return all resources that an identity can access.

Input

    user_id

Output

    (resource_id, asset_type, role)

Algorithm

1.  Collect roles assigned directly to the user.
2.  Also collect roles from groups the user belongs to.
3.  Propagate each role downward to descendant resources.

Complexity

    O(R + D)

Where

-   `R` = number of role assignments examined
-   `D` = number of descendant resources visited

------------------------------------------------------------------------

## Task 4 --- Reverse Lookup

Return all identities that have access to a resource.

Input

    resource_id

Output

    (identity_id, role)

Algorithm

1.  Start from the resource
2.  Walk **up the hierarchy**
3.  Collect incoming `HAS_ROLE` edges

Complexity

    O(h + a)

Where

-   `h` = hierarchy depth
-   `a` = number of role assignments examined

------------------------------------------------------------------------

# Example Output

## Task 2

Input:

    projects/200

Output:

    folders/100
    organizations/1

## Task 3

Input:

    user:alice@test.com

Output:

    (folders/100, Folder, roles/editor)
    (projects/200, Project, roles/editor)

## Task 4

Input:

    projects/200

Output:

    (user:alice@test.com, roles/editor)
    (group:admins@test.com, roles/owner)

------------------------------------------------------------------------

# Repository Structure

    Solution.py        Python implementation
    Solution.cpp       C++ implementation
    test.json          Example input data
    json.hpp           JSON library (C++)
    README.md

C++ project files:

    Solution.sln
    Solution.vcxproj

These are included for building the C++ version in Visual Studio.

------------------------------------------------------------------------

# Running the Python Version

Requirements

    Python 3.10+

Run:

    python Solution.py

or

    py Solution.py

------------------------------------------------------------------------

# Running the C++ Version

Requirements

-   C++17 compatible compiler
-   Visual Studio (recommended)

Build and run:

    Open Solution.sln
    Build
    Run

The C++ version includes:

-   JSON parsing (`json.hpp`)
-   Debug graph printing
-   Example queries using `test.json`

------------------------------------------------------------------------

# Input Format

The program expects JSON describing cloud assets.

Example fields:

    name
    asset_type
    ancestors
    iam_policy

Example role assignment:

    "user:alice@test.com" → roles/editor → folders/100

The parser converts this data into graph nodes and edges.

------------------------------------------------------------------------

# Design Notes

The graph stores:

    nodes
    edges
    id_to_node map

The map allows **O(1)** average lookup by node id.

Traversal uses adjacency lists stored on each node:

    out_edges
    in_edges

This enables efficient graph traversal without scanning all edges.

------------------------------------------------------------------------

# Author

Implementation created as part of a programming assignment.

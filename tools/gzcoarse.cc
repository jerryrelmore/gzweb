/*
 * Copyright 2014 Open Source Robotics Foundation
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
 *
*/

#include <gts.h>
#include <gazebo/gazebo.hh>


//////////////////////////////////////////////////
void FillVertex(GtsPoint *_p, gpointer *_data)
{
  // create a Gazebo vertex from GTS_POINT and add it to the submesh
  gazebo::common::SubMesh *subMesh =
      reinterpret_cast<gazebo::common::SubMesh *>(_data[0]);
  GHashTable* vIndex = reinterpret_cast<GHashTable *>(_data[2]);
  subMesh->AddVertex(GTS_POINT(_p)->x, GTS_POINT(_p)->y, GTS_POINT(_p)->z);
  // fill the hash table which will later be used for adding indices to the
  // submesh in the FillFace function.
  g_hash_table_insert(vIndex, _p,
      GUINT_TO_POINTER((*(reinterpret_cast<guint *>(_data[1])))++));
}

//////////////////////////////////////////////////
void FillFace(GtsTriangle *_t, gpointer *_data)
{
  gazebo::common::SubMesh *subMesh =
      reinterpret_cast<gazebo::common::SubMesh *>(_data[0]);
  GHashTable* vIndex = reinterpret_cast<GHashTable *>(_data[2]);

  GtsVertex * v1, * v2, * v3;
  gts_triangle_vertices(_t, &v1, &v2, &v3);

  subMesh->AddIndex(GPOINTER_TO_UINT(g_hash_table_lookup(vIndex, v1)));
  subMesh->AddIndex(GPOINTER_TO_UINT(g_hash_table_lookup(vIndex, v2)));
  subMesh->AddIndex(GPOINTER_TO_UINT(g_hash_table_lookup(vIndex, v3)));
}

//////////////////////////////////////////////////
void MergeVertices(GPtrArray * _vertices, double _epsilon)
{
  GPtrArray *array;
  GNode *kdtree;
  GtsVertex **verticesData = reinterpret_cast<GtsVertex **>(_vertices->pdata);
  array = g_ptr_array_new();
  for (unsigned int i = 0; i < _vertices->len; ++i)
    g_ptr_array_add(array, verticesData[i]);
  kdtree = gts_kdtree_new(array, NULL);
  g_ptr_array_free(array, true);

  // for each vertex, do a bbox kdtree search to find nearby vertices within
  // _epsilon, merge vertices if they are within the bbox
  for (unsigned int i = 0; i < _vertices->len; i++)
  {
    GtsVertex *v = reinterpret_cast<GtsVertex *>(verticesData[i]);

    // make sure vertex v is active (because they could be marked inactive
    // due to previous merge operation)
    if (!GTS_OBJECT (v)->reserved)
    {
      GtsBBox *bbox;
      GSList *selected, *j;

      // build bounding box
      bbox = gts_bbox_new(gts_bbox_class(),
          v,
          GTS_POINT(v)->x - _epsilon,
          GTS_POINT(v)->y - _epsilon,
          GTS_POINT(v)->z - _epsilon,
          GTS_POINT(v)->x + _epsilon,
          GTS_POINT(v)->y + _epsilon,
          GTS_POINT(v)->z + _epsilon);

      // select vertices which are inside bbox using kdtree
      j = selected = gts_kdtree_range(kdtree, bbox, NULL);
      while (j)
      {
        GtsVertex *sv = reinterpret_cast<GtsVertex *>(j->data);
        // mark sv as inactive (merged)
        if (sv != v && !GTS_OBJECT(sv)->reserved)
          GTS_OBJECT(sv)->reserved = v;
        j = j->next;
      }
      g_slist_free(selected);
      gts_object_destroy(GTS_OBJECT(bbox));
    }
  }

  gts_kdtree_destroy(kdtree);

  // destroy inactive vertices
  // we want to control vertex destruction
  gts_allow_floating_vertices = true;
  for (unsigned int i = 0; i < _vertices->len; ++i)
  {
    GtsVertex * v = reinterpret_cast<GtsVertex *>(verticesData[i]);
    // v is inactive
    if (GTS_OBJECT(v)->reserved)
    {
      verticesData[i] =
          reinterpret_cast<GtsVertex *>(GTS_OBJECT(v)->reserved);
      gts_object_destroy(GTS_OBJECT(v));
    }
  }
  gts_allow_floating_vertices = false;
}

//////////////////////////////////////////////////
void ConvertMeshToGTS(const gazebo::common::Mesh *_mesh, GtsSurface *_surface)
{
  if (!_surface)
  {
    gzerr << _mesh->GetName() << ": Surface is NULL\n";
    return;
  }

  GPtrArray *vertices = g_ptr_array_new();

  for (unsigned int i = 0; i < _mesh->GetSubMeshCount(); ++i)
  {
    const gazebo::common::SubMesh *subMesh = _mesh->GetSubMesh(i);
    unsigned int indexCount = subMesh->GetIndexCount();
    if (subMesh->GetVertexCount() <= 2)
      continue;

    for (unsigned int j = 0; j < subMesh->GetVertexCount(); ++j)
    {
      gazebo::math::Vector3 vertex = subMesh->GetVertex(j);
      g_ptr_array_add(vertices, gts_vertex_new(gts_vertex_class(), vertex.x,
          vertex.y, vertex.z));
    }

    // merge duplicate vertices, otherwise gts produces undesirable results
    //MergeVertices(vertices, 0.0001);

    GtsVertex **verticesData =
        reinterpret_cast<GtsVertex **>(vertices->pdata);
    for (unsigned int j = 0; j < indexCount/3; ++j)
    {
      GtsEdge *e1 = GTS_EDGE(gts_vertices_are_connected(
          verticesData[subMesh->GetIndex(3*j)],
          verticesData[subMesh->GetIndex(3*j+1)]));
      GtsEdge *e2 = GTS_EDGE(gts_vertices_are_connected(
          verticesData[subMesh->GetIndex(3*j+1)],
          verticesData[subMesh->GetIndex(3*j+2)]));
      GtsEdge *e3 = GTS_EDGE(gts_vertices_are_connected(
          verticesData[subMesh->GetIndex(3*j+2)],
          verticesData[subMesh->GetIndex(3*j)]));
      if (e1 == NULL && verticesData[subMesh->GetIndex(3*j)]
          != verticesData[subMesh->GetIndex(3*j+1)])
      {
        e1 = gts_edge_new(_surface->edge_class,
            verticesData[subMesh->GetIndex(3*j)],
            verticesData[subMesh->GetIndex(3*j+1)]);
      }
      if (e2 == NULL && verticesData[subMesh->GetIndex(3*j+1)]
          != verticesData[subMesh->GetIndex(3*j+2)])
      {
        e2 = gts_edge_new(_surface->edge_class,
            verticesData[subMesh->GetIndex(3*j+1)],
            verticesData[subMesh->GetIndex(3*j+2)]);
      }
      if (e3 == NULL && verticesData[subMesh->GetIndex(3*j+2)]
          != verticesData[subMesh->GetIndex(3*j)])
      {
        e3 = gts_edge_new(_surface->edge_class,
            verticesData[subMesh->GetIndex(3*j+2)],
            verticesData[subMesh->GetIndex(3*j)]);
      }
      if (e1 != NULL && e2 != NULL && e3 != NULL)
      {
        gts_surface_add_face(_surface, gts_face_new(_surface->face_class, e1,
            e2, e3));
      }
      else
      {
        gzwarn << _mesh->GetName() << ": Ignoring degenerate facet!";
      }
    }
  }
}


/////////////////////////////////////////////////
int main(int argc, char **argv)
{
  if (argc != 2)
  {
    gzerr << "Missing argument. Please specify a collada file as an agument"
        << std::endl;
    return -1;
  }

  // load collada mesh into Gazebo Mesh format
  const gazebo::common::Mesh *m1 =
      gazebo::common::MeshManager::Instance()->Load(argv[1]);

  GtsSurface *s1;
  GNode *tree1;
  s1 = gts_surface_new(gts_surface_class(), gts_face_class(), gts_edge_class(),
      gts_vertex_class());

  // convert Gazebo Mesh to GTS format
  ConvertMeshToGTS(m1, s1);

  /*** Do mesh simplification here ***/

  // create output mesh
  gazebo::common::Mesh *mesh = new gazebo::common::Mesh();
  gazebo::common::SubMesh *subMesh = new gazebo::common::SubMesh();
  mesh->AddSubMesh(subMesh);

  // fill the submesh with data generated by GTS
  unsigned int n;
  gpointer data[3];
  GHashTable *vIndex = g_hash_table_new(NULL, NULL);

  data[0] = subMesh;
  data[1] = &n;
  data[2] = vIndex;
  n = 0;
  gts_surface_foreach_vertex(s1, (GtsFunc)FillVertex, data);
  n = 0;
  gts_surface_foreach_face(s1, (GtsFunc)FillFace, data);
  g_hash_table_destroy(vIndex);

  // calculate normals
  mesh->RecalculateNormals();

  // may need to export mesh back to collada format here.

  // destroy surfaces
  gts_object_destroy(GTS_OBJECT(s1));

  gzerr << "Success!" << std::endl;

  return 0;
}
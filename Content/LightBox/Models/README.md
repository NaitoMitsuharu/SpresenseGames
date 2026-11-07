# Bundled particle models

Real 3D models pre-converted to particle files (`.lbpt`) with `Tools/PointCloud/ModelToPoints.py`.
The app samples these points; it never renders the original meshes. Files are staged as raw UFS files
(`Config/DefaultGame.ini`, `DirectoriesToAlwaysStageAsUFS`) and listed in `LightBoxModels.cpp`.

| File | Source | Licence |
|---|---|---|
| `island_tree_01.lbpt` | [Poly Haven: Island Tree 01](https://polyhaven.com/a/island_tree_01) (Rico Cilliers, Rob Tuytel), glTF 1k | CC0 1.0 |

To add a model: download its glTF, run
`python -I Tools/PointCloud/ModelToPoints.py <model.gltf> Content/LightBox/Models/<id>.lbpt`,
then add `<id>` and a short description to `Entries` in `Source/SpresenseMaze/LightBoxModels.cpp`.

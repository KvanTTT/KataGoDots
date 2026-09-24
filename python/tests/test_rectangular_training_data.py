import numpy as np
import pytest
import torch

from katago.train.data_processing_pytorch import (
    apply_symmetry,
    apply_symmetry_policy,
    read_npz_training_data,
)
from katago.train.modelconfigs import base_config_of_name


def write_rectangular_npz(path):
    spatial = np.zeros((1, 22, 3, 4), dtype=np.uint8)
    spatial[:, 0] = 1
    spatial[0, 3, 1, 2] = 1
    policy = np.zeros((1, 2, 13), dtype=np.int16)
    policy[0, 0, 1 * 4 + 2] = 1
    policy[0, 1, -1] = 1
    np.savez_compressed(
        path,
        binaryInputNCHWPacked=np.packbits(spatial.reshape(1, 22, 12), axis=2),
        globalInputNC=np.zeros((1, 19), dtype=np.float32),
        policyTargetsNCMove=policy,
        globalTargetsNC=np.zeros((1, 80), dtype=np.float32),
        scoreDistrN=np.zeros((1, 2 * (12 + 60)), dtype=np.int8),
        valueTargetsNCHW=np.zeros((1, 5, 3, 4), dtype=np.int8),
    )


def read_batches(path, x, y):
    return list(read_npz_training_data(
        [str(path)], batch_size=1, world_size=1, rank=0,
        pos_len_x=x, pos_len_y=y, device="cpu", randomize_symmetries=False,
        include_meta=False, model_config=base_config_of_name["b5c48h3tfr"],
    ))


def test_rectangular_npz_preserves_coordinates_and_pass(tmp_path):
    path = tmp_path / "dots.npz"
    write_rectangular_npz(path)

    (batch,) = read_batches(path, 4, 3)
    assert batch["binaryInputNCHW"].shape == (1, 22, 3, 4)
    assert batch["binaryInputNCHW"][0, 3, 1, 2] == 1
    assert batch["policyTargetsNCMove"].shape == (1, 2, 13)
    assert batch["policyTargetsNCMove"][0, 0, 1 * 4 + 2] == 1
    assert batch["policyTargetsNCMove"][0, 1, -1] == 1
    assert batch["valueTargetsNCHW"].shape == (1, 5, 3, 4)


def test_rectangular_npz_rejects_swapped_dimensions(tmp_path):
    path = tmp_path / "dots.npz"
    write_rectangular_npz(path)

    with pytest.raises(ValueError, match="check -pos-len-x and -pos-len-y"):
        read_batches(path, 3, 4)


@pytest.mark.parametrize("symmetry,expected", [
    (0, (2, 0)),
    (2, (1, 2)),
    (5, (1, 0)),
    (7, (2, 2)),
])
def test_rectangle_preserving_symmetries_keep_policy_aligned(symmetry, expected):
    spatial = torch.zeros((1, 1, 3, 4))
    spatial[0, 0, 0, 2] = 1
    policy = torch.zeros((1, 1, 13))
    policy[0, 0, 2] = 1
    policy[0, 0, -1] = 7

    spatial_result = apply_symmetry(spatial, symmetry)
    policy_result = apply_symmetry_policy(policy, symmetry, 4, 3)
    x, y = expected
    assert spatial_result.shape == (1, 1, 3, 4)
    assert spatial_result[0, 0, y, x] == 1
    assert policy_result[0, 0, y * 4 + x] == 1
    assert policy_result[0, 0, -1] == 7

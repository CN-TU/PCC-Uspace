import os

cfg = {}

cfg["REPO_DIR"] = "/home/njay2/PCC/deep-learning/"

cfg["EXPR_DIR"] = cfg["REPO_DIR"] + "experiments/"
cfg["PYTHON_ML_DIR"] = cfg["REPO_DIR"] + "python/models/gym-rpc/"
cfg["ML_MODEL_PATH"] = cfg["PYTHON_ML_DIR"]
cfg["UDT_SRC_DIR"] = cfg["REPO_DIR"] + "/src/"
cfg["UDT_LIB_DIR"] = cfg["UDT_SRC_DIR"] + "/core/"
cfg["SIM_DIR"] = cfg["REPO_DIR"] + "/sim/"

os.environ["LD_LIBRARY_PATH"] = cfg["UDT_LIB_DIR"]

PCC_CONFIG = cfg

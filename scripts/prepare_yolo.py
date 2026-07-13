import json
import shutil
from pathlib import Path

from ultralytics import YOLO


MODEL_NAME = "yolo26n.pt"
INPUT_SIZE = 640

OUTPUT_DIR = Path("../models/yolo26")


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    # 第一次运行时自动取得官方 PT 模型。
    model = YOLO(MODEL_NAME)

    # 导出固定输入尺寸的 ONNX 模型。
    exported_path = Path(
        model.export(
            format="onnx",
            imgsz=INPUT_SIZE,
            batch=1,
            dynamic=False,
            simplify=True,
        )
    )

    target_model_path = OUTPUT_DIR / exported_path.name

    if exported_path.resolve() != target_model_path.resolve():
        shutil.copy2(exported_path, target_model_path)

    # 从模型中读取正确的类别名称。
    names = model.names
    class_names = [
        names[index]
        for index in range(len(names))
    ]

    classes_path = OUTPUT_DIR / "classes.txt"
    classes_path.write_text(
        "\n".join(class_names),
        encoding="utf-8",
    )

    # 该 JSON 是 UTMS 自己使用的运行参数配置，不是官方模型文件。
    model_config = {
        "model_family": "yolo26",
        "task": "detect",
        "model_file": target_model_path.name,
        "classes_file": classes_path.name,
        "input_width": INPUT_SIZE,
        "input_height": INPUT_SIZE,
        "confidence_threshold": 0.25,
        "nms_threshold": 0.45,
        "letterbox": True,
        "swap_rb": True,
        "normalize": True,
        "normalize_scale": 1.0 / 255.0,
    }

    config_path = OUTPUT_DIR / "model.json"
    config_path.write_text(
        json.dumps(
            model_config,
            ensure_ascii=False,
            indent=4,
        ),
        encoding="utf-8",
    )

    print("Preparation completed:")
    print(f"  model:  {target_model_path}")
    print(f"  classes:{classes_path}")
    print(f"  config: {config_path}")


if __name__ == "__main__":
    main()
from flask import Flask

app = Flask(__name__)


@app.get("/")
def index():
    return {"status": "ok", "component": "fuse-proto-viz visualizer"}


if __name__ == "__main__":
    app.run(debug=True)

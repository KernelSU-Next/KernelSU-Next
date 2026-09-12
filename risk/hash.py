import json, hashlib
p = "risk.json"
with open(p, "r", encoding="utf-8") as f:
    obj = json.load(f)
rules = obj.get("rules", [])
canon = json.dumps(rules, separators=(",", ":"), ensure_ascii=False, sort_keys=True)
h = hashlib.sha256(canon.encode("utf-8")).hexdigest()
print(h)
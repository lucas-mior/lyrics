import json

with open("vocab.json", "r", encoding="utf-8") as f:
    vocab = json.load(f)

tokens = [None] * (max(vocab.values()) + 1)

for token, token_id in vocab.items():
    tokens[token_id] = token

with open("tokens.txt", "w", encoding="utf-8") as f:
    for token in tokens:
        if token is None:
            raise SystemExit("missing token id in vocab")
        f.write(token + "\n")

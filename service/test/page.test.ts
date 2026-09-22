import assert from "node:assert/strict";
import test from "node:test";
import { visitorPage } from "../src/page";

test("visitor page puts own upload before gallery and guidance", () => {
  const upload = visitorPage.indexOf('id="upload-area"');
  const gallery = visitorPage.indexOf('id="gallery"');
  const how = visitorPage.indexOf('id="how"');
  assert.ok(upload > 0 && gallery > upload && how > gallery);
  assert.ok(!visitorPage.includes("board-top.png"));
  assert.ok(visitorPage.includes('name="privateUpload" type="checkbox"'));
  assert.ok(visitorPage.includes('このゲームを非公開にする'));
  assert.ok(visitorPage.includes('id="galleryTitle"'));
  assert.ok(visitorPage.includes('name="authorName"'));
  assert.ok(visitorPage.includes('name="visitorComment"'));
  assert.ok(visitorPage.includes('name="namePublic" type="checkbox" disabled'));
  assert.ok(visitorPage.includes('あなたのゲームを<br>ファミコンへ！'));
});

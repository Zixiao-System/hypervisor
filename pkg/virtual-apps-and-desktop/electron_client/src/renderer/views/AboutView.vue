<script setup lang="ts">
</script>

<template>
  <div class="about-view">
    <h1 class="page-title">About</h1>

    <mdui-card class="about-card">
      <div class="app-info">
        <div class="app-icon">
          <svg width="80" height="80" viewBox="0 0 100 100">
            <circle cx="50" cy="50" r="45" fill="#8B5CF6"/>
            <text x="50" y="65" font-size="40" fill="white" text-anchor="middle" font-weight="bold">Z</text>
          </svg>
        </div>
        <h2 class="app-name">Zixiao Desktop</h2>
        <p class="app-version">Version 0.1.0</p>
      </div>

      <mdui-list>
        <mdui-list-item>
          <span slot="headline">Application</span>
          <span slot="description">Zixiao Hypervisor Desktop Client</span>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Framework</span>
          <span slot="description">Electron + Vue 3 + MDUI 2</span>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Theme</span>
          <span slot="description">ZiXiao System VI</span>
        </mdui-list-item>
        <mdui-list-item>
          <span slot="headline">Protocols</span>
          <span slot="description">SPICE, VNC, RDP (planned)</span>
        </mdui-list-item>
      </mdui-list>
    </mdui-card>

    <mdui-card class="about-card">
      <h3 class="card-title">Color Palette</h3>
      <div class="color-palette">
        <div class="color-item">
          <div class="color-swatch" style="background: #8B5CF6;"></div>
          <div class="color-info">
            <div class="color-name">Zixiao Purple</div>
            <div class="color-hex">#8B5CF6</div>
          </div>
        </div>
        <div class="color-item">
          <div class="color-swatch" style="background: #FCD34D;"></div>
          <div class="color-info">
            <div class="color-name">Lightning Gold</div>
            <div class="color-hex">#FCD34D</div>
          </div>
        </div>
        <div class="color-item">
          <div class="color-swatch" style="background: #204C63;"></div>
          <div class="color-info">
            <div class="color-name">Deep Space</div>
            <div class="color-hex">#204C63</div>
          </div>
        </div>
        <div class="color-item">
          <div class="color-swatch" style="background: #F5FAD8;"></div>
          <div class="color-info">
            <div class="color-name">Spirit Light</div>
            <div class="color-hex">#F5FAD8</div>
          </div>
        </div>
      </div>
    </mdui-card>

    <mdui-card class="about-card">
      <h3 class="card-title">License</h3>
      <p class="license-text">
        Zixiao Hypervisor is open source software licensed under the Apache License 2.0.
      </p>
    </mdui-card>
  </div>
</template>

<style scoped>
.about-view {
  max-width: 600px;
  margin: 0 auto;
}

.page-title {
  font-size: 28px;
  font-weight: 600;
  color: var(--zixiao-purple);
  margin-bottom: 24px;
}

.about-card {
  margin-bottom: 24px;
  padding: 24px;
}

.app-info {
  text-align: center;
  padding: 24px 0;
}

.app-icon {
  margin-bottom: 16px;
}

.app-name {
  font-size: 24px;
  font-weight: 600;
  color: var(--zixiao-purple);
  margin: 0 0 8px 0;
}

.app-version {
  font-size: 14px;
  opacity: 0.7;
  margin: 0;
}

.card-title {
  font-size: 18px;
  font-weight: 600;
  margin-bottom: 16px;
  color: var(--zixiao-purple);
}

.color-palette {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 16px;
}

.color-item {
  display: flex;
  align-items: center;
  gap: 12px;
}

.color-swatch {
  width: 40px;
  height: 40px;
  border-radius: 8px;
  border: 1px solid rgba(0, 0, 0, 0.1);
}

.color-name {
  font-size: 14px;
  font-weight: 500;
}

.color-hex {
  font-size: 12px;
  font-family: monospace;
  opacity: 0.7;
}

.license-text {
  font-size: 14px;
  line-height: 1.6;
  opacity: 0.8;
}
</style>

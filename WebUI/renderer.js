class Spring {
    constructor(stiffness = 400, damping = 29, mass = 1.0) {
        this.stiffness = stiffness;
        this.damping = damping;
        this.mass = mass;
        this.value = 0;
        this.velocity = 0;
        this.target = 0;
    }
    set(v) { this.target = v; }
    setVelocity(v) { this.velocity = v; }
    update(dt) {
        const dtSec = Math.min(dt, 1 / 30);
        const maxStep = 1 / 240;
        const steps = Math.min(8, Math.ceil(dtSec / maxStep));
        const h = dtSec / steps;
        for (let i = 0; i < steps; i++) {
            const force = (this.target - this.value) * this.stiffness;
            const dampingForce = -this.damping * this.velocity;
            const acc = (force + dampingForce) / this.mass;
            this.velocity += acc * h;
            this.value += this.velocity * h;
        }
        return this.value;
    }
}

// ============ 果冻按压 / 拖动 ============
function attachJellyDrag(el) {
    // ★ 参数微调：提高刚度加快反应，阻尼从 32 降到 17（保留轻微回弹，杜绝“黏住”感）
    const SPRING_STIFF = 380;
    const SPRING_DAMP = 17;

    // 缩放弹簧
    const springX = new Spring(SPRING_STIFF, SPRING_DAMP);
    const springY = new Spring(SPRING_STIFF, SPRING_DAMP);
    
    // ★ 平移弹簧：用来平滑处理位置偏移，彻底消除方向突变时的“抽动”
    const springTx = new Spring(SPRING_STIFF, SPRING_DAMP);
    const springTy = new Spring(SPRING_STIFF, SPRING_DAMP);

    springX.value = springX.target = 1;
    springY.value = springY.target = 1;
    springTx.value = springTx.target = 0;
    springTy.value = springTy.target = 0;

    let baseW = 0;
    let baseH = 0;

    let state = 'idle';
    let pointerId = null;
    let startX = 0, startY = 0;
    let lastX = 0, lastY = 0, lastTime = 0;
    let velocityX = 0, velocityY = 0;
    let rafId = 0, lastFrameTime = 0;

    let dirX = 0;
    let dirY = 0;

    const MAX_STRETCH = 0.15;
    const FULL_STRETCH_DIST = 120;
    const PRESS_SCALE = 1.03;

    function tick(now) {
        if (lastFrameTime === 0) lastFrameTime = now;
        const dt = Math.min(1, (now - lastFrameTime) / 1000);
        lastFrameTime = now;

        const sx = springX.update(dt);
        const sy = springY.update(dt);
        // 平滑读取位置偏移量
        const tx = springTx.update(dt);
        const ty = springTy.update(dt);

        el.style.transform = `translate(${tx}px, ${ty}px) scale(${sx}, ${sy})`;

        const settled = 
            Math.abs(springX.value - springX.target) < 0.001 && Math.abs(springX.velocity) < 0.001 &&
            Math.abs(springY.value - springY.target) < 0.001 && Math.abs(springY.velocity) < 0.001 &&
            Math.abs(springTx.value - springTx.target) < 0.001 && Math.abs(springTx.velocity) < 0.001 &&
            Math.abs(springTy.value - springTy.target) < 0.001 && Math.abs(springTy.velocity) < 0.001;

        if (!settled) {
            rafId = requestAnimationFrame(tick);
        } else {
            if (Math.abs(springX.target - 1) < 0.001 &&
                Math.abs(springY.target - 1) < 0.001) {
                el.style.transform = '';
            }
            rafId = 0;
        }
    }

    function startAnim() {
        if (rafId) return;
        if (lastFrameTime === 0 || performance.now() - lastFrameTime > 100) {
            lastFrameTime = performance.now() - 16;
        }
        rafId = requestAnimationFrame(tick);
    }

    function onWindowPointerMove(e) {
        if (e.pointerId !== pointerId) return;

        const now = performance.now();
        const dt = (now - lastTime) / 1000;
        if (dt > 0.001) {
            velocityX = (e.clientX - lastX) / dt;
            velocityY = (e.clientY - lastY) / dt;
            lastX = e.clientX;
            lastY = e.clientY;
            lastTime = now;
        }

        const dx = e.clientX - startX;
        const dy = e.clientY - startY;
        const dist = Math.sqrt(dx * dx + dy * dy);

        if (state === 'pressing' && dist > 3) {
            state = 'dragging';
        }

        if (state === 'dragging') {
            const distSafe = Math.max(dist, 1e-3);
            const unitX = dx / distSafe;
            const unitY = dy / distSafe;

            const weightX = unitX * unitX;
            const weightY = unitY * unitY;

            const stretchAmount = Math.min(dist / FULL_STRETCH_DIST, 1) * MAX_STRETCH;

            const targetSX = 1 + stretchAmount * weightX - stretchAmount * 0.35 * weightY;
            const targetSY = 1 + stretchAmount * weightY - stretchAmount * 0.35 * weightX;

            springX.set(Math.max(0.7, targetSX));
            springY.set(Math.max(0.7, targetSY));

            const signX = Math.sign(dx);
            const signY = Math.sign(dy);

            // ★ 给平移目标也设定弹簧，让它平滑追踪，不再硬切
            springTx.set(signX * (targetSX - 1) * baseW / 2);
            springTy.set(signY * (targetSY - 1) * baseH / 2);

            dirX = unitX;
            dirY = unitY;

            startAnim();
        }
    }

    function onWindowPointerUp(e) {
        if (e.pointerId !== pointerId) return;

        window.removeEventListener('pointermove', onWindowPointerMove);
        window.removeEventListener('pointerup', onWindowPointerUp);
        window.removeEventListener('pointercancel', onWindowPointerUp);

        pointerId = null;
        state = 'idle';

        springX.set(1);
        springY.set(1);
        springTx.set(0);
        springTy.set(0);

        // 稍微放大最大初速度，让松手那一下有“嘣”的利落感，但阻尼 17 会很快拉住它
        const maxVel = 3.0;
        springX.setVelocity(Math.max(-maxVel, Math.min(maxVel, velocityX / 400)));
        springY.setVelocity(Math.max(-maxVel, Math.min(maxVel, velocityY / 400)));

        startAnim();
    }

    function onPointerDown(e) {
        if (e.button !== 0 || state !== 'idle') return;
        e.preventDefault();

        const r = el.getBoundingClientRect();
        baseW = r.width;
        baseH = r.height;
        if (baseW <= 0 || baseH <= 0) return;

        pointerId = e.pointerId;
        state = 'pressing';
        startX = lastX = e.clientX;
        startY = lastY = e.clientY;
        lastTime = performance.now();
        velocityX = velocityY = 0;
        dirX = 0;
        dirY = 0;

        springX.set(PRESS_SCALE);
        springY.set(PRESS_SCALE);
        springTx.set(0);
        springTy.set(0);
        startAnim();

        window.addEventListener('pointermove', onWindowPointerMove);
        window.addEventListener('pointerup', onWindowPointerUp);
        window.addEventListener('pointercancel', onWindowPointerUp);
    }

    el.addEventListener('pointerdown', onPointerDown);
}

// 所有 .box 按钮（开始复制 / 保存）使用纯 DOM 果冻交互。
document.querySelectorAll('.box').forEach(el => attachJellyDrag(el));


function setupCustomDropdowns() {
    const selects = document.querySelectorAll('.select-wrapper select.m4-select');
    if (!selects.length) return;

    // 取原生 value 访问器，之后包一层拦截：任何代码程序化赋值都同步刷新自绘按钮
    const protoDesc = Object.getOwnPropertyDescriptor(HTMLSelectElement.prototype, 'value');

    function build(select) {
        const wrapper = select.closest('.select-wrapper');
        if (!wrapper || wrapper.dataset.ddInit) return;
        wrapper.dataset.ddInit = '1';

        select.classList.add('dd-native');
        select.tabIndex = -1;

        const btn = document.createElement('button');
        btn.type = 'button';
        btn.className = 'dd-button';
        btn.setAttribute('aria-haspopup', 'listbox');
        btn.setAttribute('aria-expanded', 'false');
        const label = document.createElement('span');
        label.className = 'dd-label';
        btn.appendChild(label);

        const panel = document.createElement('div');
        panel.className = 'dd-panel';
        panel.hidden = true;

        wrapper.appendChild(btn);
        wrapper.appendChild(panel);

        function refreshLabel() {
            const i = select.selectedIndex;
            label.textContent = i >= 0 ? select.options[i].textContent : '';
        }

        // 拦截 value：原生行为不变，只是赋值后刷新按钮文字
        Object.defineProperty(select, 'value', {
            configurable: true,
            get() { return protoDesc.get.call(select); },
            set(v) {
                protoDesc.set.call(select, v);
                refreshLabel();
            }
        });

        // applyLang 会改写 <option> 文案（策略项随语言变化），监听后同步刷新
        const mo = new MutationObserver(refreshLabel);
        for (const opt of select.options) {
            mo.observe(opt, { childList: true, characterData: true, subtree: true });
        }

        function closeAll() {
            document.querySelectorAll('.dd-panel:not([hidden])').forEach(p => {
                p.hidden = true;
                const w = p.closest('.select-wrapper');
                const b = w && w.querySelector('.dd-button');
                if (b) b.setAttribute('aria-expanded', 'false');
            });
        }

        function open() {
            closeAll();
            panel.innerHTML = '';
            const current = select.value;
            for (const opt of select.options) {
                const item = document.createElement('div');
                item.className = 'dd-opt' + (opt.value === current ? ' selected' : '');
                item.textContent = opt.textContent;
                // 使用 mousedown 防止当面板隐藏时 select 状态被意外重置
                item.addEventListener('mousedown', (e) => {
                    e.preventDefault();
                    e.stopPropagation();
                    select.value = opt.value; // 走上面的拦截 setter
                    select.dispatchEvent(new Event('change', { bubbles: true }));
                    closeAll();
                });
                panel.appendChild(item);
            }
            panel.hidden = false;
            btn.setAttribute('aria-expanded', 'true');
        }

        btn.addEventListener('click', (e) => {
            e.stopPropagation(); // 阻止冒泡到 document 的 click
            if (panel.hidden) open(); else closeAll();
        });
        btn.addEventListener('keydown', (e) => {
            if (e.key === 'Escape') { closeAll(); e.preventDefault(); }
            else if (e.key === 'ArrowDown' && panel.hidden) { e.preventDefault(); open(); }
        });
        
        // 监听 document 的 click 以关闭面板（不会受到 pointerdown 冲突影响）
        document.addEventListener('click', (e) => {
            if (!wrapper.contains(e.target)) closeAll();
        });

        refreshLabel();
    }

    selects.forEach(build);
}

setupCustomDropdowns();

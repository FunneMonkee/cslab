mod api;
mod model;
mod repo;

use api::user::{create_user, delete_user, get_user, update_user};

use actix_web::{App, HttpServer, middleware::Logger, web::Data};
use repo::mongo_db::MongoRepo;

#[actix_web::main]
async fn main() -> std::io::Result<()> {
    unsafe {
        std::env::set_var("RUST_LOG", "debug");
    }
    unsafe {
        std::env::set_var("RUST_BACKTRACE", "1");
    }
    env_logger::builder()
        .filter_level(log::LevelFilter::Info)
        .init();

    HttpServer::new(move || {
        let logger = Logger::default();
        let db = MongoRepo::init();
        App::new()
            .app_data(Data::new(db))
            .wrap(logger)
            .service(create_user)
            .service(get_user)
            .service(update_user)
            .service(delete_user)
    })
    .bind(("0.0.0.0", 8080))?
    .run()
    .await
}

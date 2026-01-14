use crate::{model::dispense_log::DispenseLog, repo::mongo_db::MongoRepo};
use actix_web::{
    HttpResponse, delete,
    error::ResponseError,
    get,
    http::{StatusCode, header::ContentType},
    post,
    web::{Data, Json, Path},
};
use derive_more::Display;
use mongodb::results::{DeleteResult, InsertOneResult};

#[derive(Debug, Display)]
pub enum DispenseError {
    DispenseCreationFailure,
    DispenseNotFound,
    DispenseDeletionFailure,
}

impl ResponseError for DispenseError {
    fn error_response(&self) -> HttpResponse {
        HttpResponse::build(self.status_code())
            .insert_header(ContentType::json())
            .body(self.to_string())
    }

    fn status_code(&self) -> StatusCode {
        match self {
            DispenseError::DispenseCreationFailure => StatusCode::FAILED_DEPENDENCY,
            DispenseError::DispenseNotFound => StatusCode::NOT_FOUND,
            DispenseError::DispenseDeletionFailure => StatusCode::FAILED_DEPENDENCY,
        }
    }
}

#[post("dispenses")]
pub async fn create_dispense(
    db: Data<MongoRepo>,
    log: Json<DispenseLog>,
) -> Result<Json<InsertOneResult>, DispenseError> {
    match db.create_dispense_log(log.0) {
        Ok(result) => Ok(Json(result)),
        Err(_) => Err(DispenseError::DispenseCreationFailure),
    }
}

#[get("dispenses")]
pub async fn get_all_dispenses(
    db: Data<MongoRepo>,
) -> Result<Json<Vec<DispenseLog>>, DispenseError> {
    match db.get_all_dispense_logs() {
        Ok(logs) => Ok(Json(logs)),
        Err(_) => Err(DispenseError::DispenseNotFound),
    }
}
